#include "skeet/lagcomp/history_animation_publish.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/animation/layer_state.hpp"
#include "skeet/resolver.hpp"
#include "skeet/resolver_runtime/movement_fit.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
float clamp_unit(float value) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_setzero_ps()), _mm_set_ss(1.0f)));
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (std::fabs(value) > 180.0f) {
        const auto turns = round_half_away_from_zero(std::fabs(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + turns : value - turns;
    }
    return value;
}
float quantize_yaw(float value) noexcept {
    const auto scaled = value * 182.0444488525390625f;
    const auto integer = _mm_cvttss_si32(_mm_set_ss(scaled));
    return float(std::uint32_t(integer) & 0xFFFFu) * 0.0054931640625f;
}
//433FD134 with the actual fixed inputs ECX=8,XMM1=0,XMM2=1. Its scale is
// exactly255 and no scale-retry arm is entered. FISTP obeys the x87 rounding
// mode; nearbyint preserves the active native floating-point rounding mode.
float quantize_weight(float value) noexcept {
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    const auto scaled = value * 255.0f;
    return float(std::nearbyint(static_cast<double>(scaled))) / 255.0f;
}
float length2(const Vector& value) noexcept {
    return std::sqrt(value[0] * value[0] + value[1] * value[1]);
}
float length3(const Vector& value) noexcept {
    return std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]);
}
void* studio(void* player, const HistoryAnimationPublishBindings& bindings) noexcept {
    auto* wrapper = read<void*>(player, bindings.fields.studio_header);
    return wrapper && read<void*>(wrapper, 0) ? wrapper : nullptr;
}
float local_time(const HistoryAnimationPublishBindings& bindings) noexcept {
    if (!bindings.local_time_entity || !*bindings.local_time_entity)
        return 0.0f;
    return read<float>(*bindings.local_time_entity, bindings.fields.local_time);
}
//Complete433D716B. Pose writes are mirrored into both live interpolation
// destinations selected by the native16-bit cyclic watcher indices.
void publish_pose(void* poses, std::uint32_t index) noexcept {
    const auto first = read<std::uint16_t>(poses, 0x6C);
    const auto last = read<std::uint16_t>(poses, 0x6E);
    const auto relative = last < first ? std::uint32_t(last) : std::uint32_t(last - first);
    const auto* entries = read<const std::byte*>(poses, 0x68);
    auto* current = read<void*>(entries, std::size_t(relative) * 0xC + 8);
    const auto value = read<std::uint32_t>(poses, index * 4u);
    if (current)
        write(current, index * 4u, value);
    if (auto* destination = read<void*>(poses, 0x74))
        write(destination, index * 4u, value);
}
struct MovementCandidate {
    std::uint32_t ordinal;
    std::uint32_t seed;
    float movement_yaw;
    float feet_yaw;
    float pose_yaw;
    float score;
};
bool movement_eligible(client::PlayerState& owner, void* player, void* state,
                       std::int32_t simulation_tick, std::int32_t simulation_ticks,
                       std::uint32_t update_flags,
                       const HistoryAnimationPublishBindings& bindings) {
    const auto* native = &owner.native;
    if (!*bindings.configuration.resolver_enabled ||
        !bindings.player_table[bindings.player_table_offset + 6] || !owner.history.count)
        return false;
    const auto resolver = owner.native.resolver_index;
    if (resolver < 1 || resolver > 3)
        return false;
    const auto sequence = read<std::uint32_t>(native, 0x3A8);
    if (sequence != read<std::uint32_t>(native, 0x514) &&
        (sequence || !bindings.allow_zero_sequence))
        return false;
    if (0.000099999997473787516f >= read<float>(native, 0x51C))
        return false;
    if (animation::layer_is_active(reinterpret_cast<const std::byte*>(native) + 0x4F4) &&
        read<float>(native, 0x500) != 0.0f)
        return false;
    if (local_time(bindings) == owner.native.reference_time &&
        std::fabs(*bindings.now - read<float>(native, 0xCC)) < 5.0f)
        return false;
    if (!bindings.movement.displacement || !state ||
        !(read<std::uint8_t>(player, bindings.fields.player_flags) & 1u))
        return false;
    const auto receive = sample_receive_tick(bindings.clock, bindings.receive_interval);
    if (read<std::int32_t>(native, 0x1DC) != receive &&
        ((update_flags & 1u) || 0.0005000000237487257f >= read<float>(native, 0x524)))
        return false;
    const auto& previous = owner.history.at(0).header;
    if (!(previous.player_flags & 1u))
        return false;
    const auto blend = read<float>(native, 0x5A8);
    if ((update_flags & 8u) || simulation_ticks < 2 || (blend > 0.0f && blend < 1.0f))
        return true;
    const auto interval = bindings.entity.interval;
    if (time_to_tick(read<float>(player, bindings.fields.old_simulation_time), interval) !=
        simulation_tick)
        return false;
    if (read<float>(native, 0x3B0) - read<float>(native, 0x51C) > interval + interval)
        return false;
    const auto live = read<Vector>(player, bindings.fields.absolute_velocity);
    if (std::fabs(length2(previous.velocity) - length2(live)) >= interval * 2000.0f)
        return false;
    if (read<float>(native, 0x5C4) > 0.009999999776482582f) {
        const auto old_length = length3(previous.velocity);
        const auto live_length = length3(live);
        const auto old_scale = 1.0f / (old_length + std::numeric_limits<float>::epsilon());
        const auto live_scale = 1.0f / (live_length + std::numeric_limits<float>::epsilon());
        if (std::fabs(old_length - live_length) > 5.0f)
            return false;
        const float x = previous.velocity[0] * old_scale;
        const float y = previous.velocity[1] * old_scale;
        const float z = previous.velocity[2] * old_scale;
        const auto dot =
            (live[1] * live_scale * y + live[0] * live_scale * x) + live[2] * live_scale * z;
        if (!(dot >= 0.9993908405303955f))
            return false;
    }
    return true;
}
void resolve_movement(client::PlayerState& owner, void* player, void* state,
                      std::int32_t simulation_tick, float speed_fraction,
                      std::uint32_t& update_flags,
                      const HistoryAnimationPublishBindings& bindings) {
    if (!studio(player, bindings) || !studio(player, bindings))
        return;
    const bool reworked = bindings.configuration.reworked();
    auto* native = &owner.native;
    const auto running = read<float>(state, 0xF8);
    const auto ducking = read<float>(state, 0xFC);
    const auto animation_fraction = read<float>(state, 0x11C);
    const auto horizontal_speed = read<float>(state, 0xEC);
    const auto duck = read<float>(state, 0xA4);
    auto weight = read<float>(native, 0x51C);
    if (reworked)
        for (float value : std::array{running, ducking, animation_fraction, horizontal_speed, duck,
                                      weight, speed_fraction})
            if (!std::isfinite(value))
                return;
    bool angled = false;
    float cosine = 0.0f;
    if (weight < 1.0f && read<float>(native, 0x524) != 0.0f) {
        const auto layer_weight = read<float>(native, 0x500);
        if (layer_weight > 0.0f && read<float>(state, 0x124) > 0.0f)
            weight = (weight / maximum_number(1.0f - layer_weight, 0.550000011920929f)) /
                     read<float>(state, 0x124);
        const auto run = clamp_unit(running);
        const auto blend = (clamp_unit(ducking) - run) * duck + run;
        if (reworked && !(blend > 0.0f))
            return;
        cosine = clamp_unit(bindings.power(weight / blend, bindings.movement_exponent));
        angled = cosine < 0.9902681112289429f;
    }
    if (angled != bool(read<std::uint8_t>(native, 0xE4)) || (update_flags & 1u)) {
        write(native, 0xE4, std::uint8_t(angled));
        write(native, 0xE8, UINT32_MAX);
    }
    std::array<float, 4> yaws;
    std::size_t yaw_count = 0;
    if (angled) {
        const auto first = bindings.arccosine(cosine) * 57.2957763671875f;
        const auto second = bindings.arccosine(negate(cosine)) * 57.2957763671875f;
        yaws[yaw_count++] = first;
        yaws[yaw_count++] = negate(first);
        yaws[yaw_count++] = second;
        yaws[yaw_count++] = negate(second);
    } else
        yaws[yaw_count++] = 0.0f;
    auto* poses = static_cast<std::byte*>(player) + bindings.fields.poses;
    const auto saved_pose = read<float>(poses, 0x1C);
    const auto sequence = read<std::int32_t>(native, 0x514);
    float expected_speed = 0.0f;
    if (!reworked && read<float>(native, 0x524) > 0.0f) {
        const auto playback =
            engine::invoke<float>(player, 222, studio(player, bindings), sequence);
        const auto cycle = ((read<float>(native, 0x524) / bindings.entity.interval) /
                            (1.0f - animation_fraction * 0.15000000596046448f)) /
                           playback;
        expected_speed = (horizontal_speed / cycle) * (1.0f / playback);
    }
    if (reworked) {
        const auto rate = read<float>(native, 0x524);
        if (!(rate > 0.0f) || !std::isfinite(rate) || !std::isfinite(horizontal_speed) ||
            !std::isfinite(animation_fraction))
            return;
        // Playback appears once in the numerator and once in the denominator.
        // Cancel it before evaluation rather than producing 0/0 for a paused sequence.
        expected_speed = horizontal_speed * (1.0f - animation_fraction * 0.15f) *
                         bindings.entity.interval / rate;
        if (!(expected_speed > 0.0f) || !std::isfinite(expected_speed))
            return;
    }
    //The second native slot222 call is unconditional even though its returned
    //playback value is discarded.
    (void)engine::invoke<float>(player, 222, studio(player, bindings), sequence);
    float modifier = ((0.800000011920929f - speed_fraction * 0.30000001192092896f) - 1.0f) *
                         clamp_unit(running) +
                     1.0f;
    if (duck > 0.0f)
        modifier += clamp_unit(ducking) * duck * (0.5f - modifier);
    const auto maximum_yaw = read<float>(state, 0x338) * modifier;
    const auto velocity = read<Vector>(player, bindings.fields.absolute_velocity);
    const auto backward_x = negate(velocity[0]);
    const auto backward_y = negate(velocity[1]);
    const auto direction =
        backward_x == 0.0f && backward_y == 0.0f
            ? 0.0f
            : float(bindings.atan2(double(backward_y), double(backward_x))) * 57.2957763671875f;
    const std::array<float, 3> seeds{negate(maximum_yaw), 0.0f, maximum_yaw};
    MovementCandidate best{};
    best.score = 10.0f;
    std::uint32_t ordinal = 0;
    bool retain = false;
    if (reworked) {
        const auto minimum_yaw = read<float>(state, 0x334) * modifier;
        const auto candidate = [&](float pose_yaw, unsigned branch) {
            const auto feet_yaw = normalize(quantize_yaw(owner.native.reference_yaw - pose_yaw));
            const auto movement_yaw = normalize(direction - feet_yaw - yaws[branch]);
            const auto seed = pose_yaw < -maximum_yaw * 0.5f  ? 0u
                              : pose_yaw > maximum_yaw * 0.5f ? 2u
                                                              : 1u;
            return MovementCandidate{UINT32_MAX, seed, movement_yaw, feet_yaw, pose_yaw, 0.0f};
        };
        const auto fit = resolver_runtime::fit_movement_pose(
            minimum_yaw, maximum_yaw, unsigned(yaw_count), 0.25f, [&](float pose, unsigned branch) {
                const auto value = candidate(pose, branch);
                write(poses, 0x1C, clamp_unit((value.movement_yaw + 180.0f) / 360.0f));
                Vector displacement{};
                bindings.movement.displacement(bindings.movement.context, studio(player, bindings),
                                               sequence, reinterpret_cast<const float*>(poses),
                                               displacement);
                return std::fabs(length3(displacement) - expected_speed);
            });
        if (fit.identified) {
            best = candidate(fit.pose, fit.branch);
            best.score = fit.error;
        }
    } else
        for (std::uint32_t seed = 0; seed < 3 && !retain; ++seed) {
            auto pose_yaw = seeds[seed];
            if (pose_yaw != 0.0f) {
                const auto step =
                    std::fabs((speed_fraction * 20.0f + 30.0f) * bindings.entity.interval);
                const auto delta = normalize(0.0f - pose_yaw);
                if (delta > step)
                    pose_yaw += step;
                else if (delta < negate(step))
                    pose_yaw -= step;
                else
                    pose_yaw = 0.0f;
            }
            const auto feet_yaw = normalize(quantize_yaw(owner.native.reference_yaw - pose_yaw));
            const auto move_delta = direction - feet_yaw;
            for (auto* cursor = yaws.data(); cursor != yaws.data() + yaw_count;
                 ++cursor, ++ordinal) {
                const auto movement_yaw = normalize(move_delta - *cursor);
                write(poses, 0x1C, clamp_unit((movement_yaw - -180.0f) / 360.0f + 0.0f));
                //43347018->433E065C: concrete displacement-length calculation.
                Vector displacement{};
                bindings.movement.displacement(bindings.movement.context, studio(player, bindings),
                                               sequence, reinterpret_cast<const float*>(poses),
                                               displacement);
                const auto score = std::fabs(length3(displacement) - expected_speed);
                const MovementCandidate candidate{ordinal,  seed,     movement_yaw,
                                                  feet_yaw, pose_yaw, score};
                retain = owner.native.resolver_status == 2 &&
                         ordinal == read<std::uint32_t>(native, 0xE8) && score < 3.0f;
                if (retain || score < best.score)
                    best = candidate;
                if (retain)
                    break;
            }
        }
    write(poses, 0x1C, saved_pose);
    if (best.score < 10.0f) {
        //PUSH7 at43416218 moves the stack before DIVSS[ESP+34]; the source
        //operand is the enclosing frame's360 constant at stack+30.
        write(poses, 0x1C, clamp_unit((best.movement_yaw - -180.0f) / 360.0f + 0.0f));
        write(poses, 0x2C, clamp_unit((best.pose_yaw - -60.0f) / 120.0f + 0.0f));
        publish_pose(poses, 7);
        publish_pose(poses, 11);
        if (bindings.entity.set_absolute_angles)
            bindings.entity.set_absolute_angles(player, Vector{0.0f, best.feet_yaw, 0.0f});
        write(state, 0x80, best.feet_yaw);
        write(state, 0x88, best.movement_yaw);
        update_flags |= 0x20u;
        owner.native.reference_time = local_time(bindings);
        owner.native.resolver_status = 2;
        owner.native.reference_origin = read<Vector>(player, bindings.fields.player_origin);
        owner.native.resolver_slots[owner.native.resolver_index].attempt =
            bindings.candidate_attempts[best.seed];
        write(native, 0xE8, best.ordinal);
        write(native, 0xEC, simulation_tick);
    } else
        write(native, 0xE8, UINT32_MAX);
    //433CCE8E is the empty fixed-capacity-list destructor.
}
}
void publish_history_animation(client::PlayerState& owner, void* player, void* state,
                               std::int32_t simulation_tick, std::int32_t simulation_ticks,
                               float speed_fraction, float layer_weight,
                               std::uint32_t& update_flags,
                               const HistoryAnimationPublishSaved& saved,
                               const HistoryAnimationPublishBindings& bindings) {
    const auto& entity = bindings.entity;
    if (bindings.player_table[bindings.player_table_offset + 5])
        write(
            state, 0x80,
            owner.native.reference_yaw -
                float(read<std::int32_t>(bindings.player_table, bindings.player_table_offset + 8)));
    *saved.current_time = float(simulation_tick) * entity.interval;
    const auto previous = read<std::uint8_t>(player, bindings.fields.animation_enabled);
    write(player, bindings.fields.animation_enabled, read<std::uint8_t>(&owner.native, 0x17C));
    write(state, 0x70,
          std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(entity.frame) - 1u));
    if (read<float>(state, 0x6C) == *saved.current_time)
        write(state, 0x6C, entity.interval + *saved.current_time);
    engine::invoke<void>(player, 224);
    if (bindings.reset_byte)
        *bindings.reset_byte = 0;
    write(player, bindings.fields.animation_enabled, previous);
    const auto weight = read<float>(state, 0x9C);
    const auto blended =
        (maximum_number(1.0f - layer_weight, 0.550000011920929f) * read<float>(state, 0x124)) *
        weight;
    if (std::fabs(quantize_weight(clamp_unit(blended)) - read<float>(&owner.native, 0x51C)) >
        0.000099999997473787516f)
        write(state, 0x9C, read<float>(&owner.native, 0x51C));
    const auto* descriptor =
        static_cast<const std::byte*>(player) + entity.offsets.animation_layers;
    auto* layers = read<std::byte*>(descriptor, 0);
    auto count = read<std::int32_t>(descriptor, 0xC);
    if (count > 13)
        count = 13;
    for (std::int32_t index = 0; index < count; ++index)
        std::memcpy(layers + std::size_t(index) * 0x38 + 0x14,
                    reinterpret_cast<const std::byte*>(&owner.native) + 0x468 +
                        std::size_t(index) * 0x1C,
                    0x1C);
    write(state, 0x98, read<std::uint32_t>(&owner.native, 0x528));
    write(state, 0x19C, read<std::uint32_t>(&owner.native, 0x530));
    write(state, 0x190, read<std::uint32_t>(&owner.native, 0x538));
    write(state, 0x198, read<std::uint32_t>(&owner.native, 0x544));
    write(state, 0x180, read<std::uint32_t>(&owner.native, 0x5C4));
    if (movement_eligible(owner, player, state, simulation_tick, simulation_ticks, update_flags,
                          bindings))
        resolve_movement(owner, player, state, simulation_tick, speed_fraction, update_flags,
                         bindings);
    write(player, bindings.fields.invalidation_counter, std::uint32_t(0));
    write(player, entity.offsets.last_bone_setup_time, std::uint32_t(0xFF7FFFFF));
    write(player, entity.offsets.field_0471e13c, std::uint32_t(0));
    write(player, entity.offsets.effects_bb78dc43, std::uint32_t(4));
    *saved.frame_time = saved.saved_frame_time;
    *saved.current_time = saved.saved_current_time;
    publish_history_animation_origin(owner, player, saved.absolute_origin, bindings);
}
void publish_history_animation_origin(client::PlayerState& owner, void* player,
                                      const Vector& saved_absolute_origin,
                                      const HistoryAnimationPublishBindings& bindings) {
    const auto origin = *engine::invoke<const Vector*>(static_cast<std::byte*>(player) + 4, 2);
    write(player, bindings.fields.render_origin, origin);
    const auto& entity = bindings.entity;
    if (entity.set_absolute_origin) {
        const auto offset = entity.offsets.transform_guard_49d93e62 - 0x14u;
        const auto guard = read<std::uint32_t>(player, offset);
        write(player, offset, UINT32_MAX);
        entity.set_absolute_origin(player, saved_absolute_origin);
        write(player, offset, guard);
    }
    if (read<float>(static_cast<const std::byte*>(player) + bindings.fields.poses, 0x30) >= 0.75f)
        write(&owner.native, 0x160, *bindings.now);
}
} //namespace skeet::lagcomp
