#include "skeet/lagcomp/history_alternative.hpp"
#include "skeet/engine/opaque_native_word.hpp"
#include "skeet/lagcomp/packet_capture.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/movement/steering_math.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class T> void write(void* object, std::uint32_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t wrapped_absolute(std::int32_t value) noexcept {
    const auto sign = value < 0 ? UINT32_MAX : 0u;
    return std::bit_cast<std::int32_t>((std::uint32_t(value) ^ sign) - sign);
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
const void* terminal(const void* variable) noexcept {
    for (;;) {
        const auto parent = read<const void*>(variable, 0x1c);
        if (parent == variable)
            return variable;
        variable = parent;
    }
}
float variable_float(const void* variable) noexcept {
    variable = terminal(variable);
    return std::bit_cast<float>(read<std::uint32_t>(variable, 0x2c) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(variable)));
}
std::int32_t variable_integer(const void* variable) noexcept {
    variable = terminal(variable);
    return std::bit_cast<std::int32_t>(read<std::uint32_t>(variable, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(variable)));
}
std::int32_t prediction_limit(const HistoryAlternativePredictionLimit& bindings) noexcept {
    void* const local = bindings.local_player ? *bindings.local_player : nullptr;
    if (local) {
        if (!read<std::uint8_t>(local, bindings.field_bf567619))
            return *bindings.configured;
        if (read<std::uint8_t>(local, bindings.field_dda99ab5) &&
            read<std::int32_t>(local, bindings.field_b7ea8fda) == 1)
            return 3;
    }
    return 7;
}
std::int32_t cap(std::int32_t value, std::int32_t limit) noexcept {
    if (limit < 1)
        return limit;
    if (value < 1)
        return 1;
    return value > limit ? limit : value;
}
float stamina_clamp(float value, float maximum) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_setzero_ps()), _mm_set_ss(maximum)));
}
float planar_yaw(const Vector& velocity, const HistoryAlternativeBindings& bindings) {
    if (velocity[1] == 0.0f && velocity[0] == 0.0f)
        return 0.0f;
    return static_cast<float>(bindings.atan2_import(double(velocity[1]), double(velocity[0]))) *
           57.2957763671875f;
}
struct Motion {
    std::int32_t tick;
    std::uint32_t flags;
    Vector origin, velocity;
    float cosine, sine, heading, stamina;
};
static_assert(sizeof(Motion) == 48);
float squared_distance(const Vector& a, const Vector& b) noexcept {
    const float y = a[1] - b[1], x = a[0] - b[0], z = a[2] - b[2];
    return (y * y + x * x) + z * z;
}
MovementVector vector(const Vector& value) noexcept {
    return {value[0], value[1], value[2]};
}
Vector vector(MovementVector value) noexcept {
    return {value.x, value.y, value.z};
}
Vector advance(const Vector& origin, const Vector& velocity, float time) noexcept {
    return {velocity[0] * time + origin[0], velocity[1] * time + origin[1],
            velocity[2] * time + origin[2]};
}
std::size_t make_pattern(std::array<std::int32_t, 10>& pattern, const LagTimeline& timeline,
                         const HistoryAlternativeBindings& bindings) {
    std::size_t count = 0;
    bool simple = timeline.record_count < 2 || timeline.packet_count < 2;
    const auto simulation = time_to_tick(timeline.simulation_time, *bindings.interval);
    if (!simple && simulation < sub(timeline.last_update_tick, 1) &&
        simulation < timeline.at(0).header.simulation_tick)
        simple = true;
    if (!simple && packet_timing_discontinuity(timeline))
        simple = true;
    if (!simple && (timeline.at(0).header.flags & 0x200u))
        simple = true;
    if (!simple) {
        const auto head = timeline.packet_head % 6u, next = (timeline.packet_head + 1u) % 6u;
        if (sub(timeline.packets[head].field_18, timeline.packets[next].field_18) < 4 &&
            sub(timeline.at(0).header.receive_tick, timeline.at(1).header.receive_tick) > 7)
            simple = true;
    }
    if (simple)
        pattern[count++] = 2;
    else {
        const LagRecord* previous = nullptr;
        for (std::uint32_t age = 1; age < timeline.record_count; ++age) {
            const auto& record = timeline.at(age);
            if (previous && !(previous->header.flags & 8u)) {
                const auto limit = prediction_limit(bindings.prediction);
                pattern[count++] = cap(
                    sub(previous->header.simulation_tick, record.header.simulation_tick), limit);
                if (count == pattern.size())
                    break;
            }
            if (record.header.flags & 0x200u)
                break;
            previous = &record;
        }
        const auto limit = prediction_limit(bindings.prediction);
        const auto latest =
            cap(sub(timeline.at(0).header.simulation_tick, timeline.at(1).header.simulation_tick),
                limit);
        const auto arrival_limit = prediction_limit(bindings.prediction);
        const auto received_tick = sample_receive_tick(bindings.engine, *bindings.receive_interval);
        const auto arrival =
            cap(add(sub(received_tick, timeline.at(0).header.receive_tick), 1), arrival_limit);
        if (count < pattern.size())
            pattern[count++] = latest >= arrival ? latest : arrival;
    }
    return count;
}
} // namespace

std::int32_t model_alternative_history_record(client::CommandFrame& context, void* player,
                                              client::PlayerState& owner, LagRecord& destination,
                                              std::int32_t ticks,
                                              const HistoryAlternativeBindings& bindings) {
    const auto& newest = owner.history.at(0).header;
    const auto initial_tick = newest.simulation_tick;
    const auto now = add(std::bit_cast<std::int32_t>(context.field_d4),
                         std::bit_cast<std::int32_t>(context.field_18c));
    (void)engine::invoke<float>(bindings.engine, 14);
    (void)engine::invoke<float>(bindings.engine, 14);
    const auto timeline = history_timeline(owner);
    if (wrapped_absolute(sub(now, initial_tick)) >= 128)
        return 0;

    Motion motion;
    motion.tick = initial_tick;
    if (!(owner.native.flags & 0x100u)) {
        motion.origin = newest.origin;
        motion.velocity = newest.velocity;
        motion.flags = newest.player_flags;
    } else {
        const auto origin = owner.native.cached_origin;
        motion.origin = origin;
        motion.velocity = read<Vector>(player, bindings.fields.velocity_36375c23);
        motion.flags = read<std::uint32_t>(player, bindings.fields.player_flags_e2fbe695);
    }
    float turn = 0.0f;
    if (owner.history.count >= 2) {
        const auto& previous = owner.history.at(1).header;
        const float elapsed =
            float(sub(newest.simulation_tick, previous.simulation_tick)) * (*bindings.interval);
        const float newest_yaw = planar_yaw(newest.velocity, bindings);
        const float previous_yaw = planar_yaw(previous.velocity, bindings);
        turn =
            (normalize_movement_angle(newest_yaw - previous_yaw) / elapsed) * (*bindings.interval);
    }
    if (absolute(turn) > 6.0f)
        turn = 0.0f;
    motion.heading = planar_yaw(motion.velocity, bindings);
    const float heading_radians = motion.heading * 0.01745329238474369f;
    motion.cosine = bindings.trigonometry.cosine(heading_radians);
    motion.sine = bindings.trigonometry.sine(heading_radians);
    motion.stamina = 0.0f;

    auto* const scratch = bindings.animation_scratch.data();
    animation::initialize_animation_state_vector(scratch);
    void* const original_state = read<void*>(player, bindings.offsets.animation_state);
    if (original_state)
        std::memcpy(scratch, original_state, bindings.animation_scratch.size());
    write(player, bindings.offsets.animation_state, static_cast<void*>(scratch));
    const auto saved_angles = *engine::invoke<const Vector*>(player, 11);
    const bool owner_mode = ((owner.native.flags >> 1) & 1u) != 0;
    const float turn_radians = turn * 0.01745329238474369f;
    const float turn_cosine = bindings.trigonometry.cosine(turn_radians);
    const float turn_sine = bindings.trigonometry.sine(turn_radians);
    std::array<std::int32_t, 10> pattern;
    const auto pattern_count = make_pattern(pattern, timeline, bindings);

    bool has_cutoff = false;
    std::int32_t cutoff = 0;
    if ((context.flags & 0x4000u) && (owner.native.flags & 1u)) {
        const auto handle = read<std::uint32_t>(player, bindings.offsets.weapon_handle);
        if (handle != UINT32_MAX) {
            if (void* const weapon = engine::invoke<void*>(bindings.entity_list, 4, handle)) {
                const float shot_tick = read<float>(weapon, bindings.fields.weapon_time_0176a381) /
                                            (*bindings.interval) +
                                        0.5f;
                const float duration = engine::invoke<float>(weapon, 433, std::int32_t(0));
                const float quotient = duration / (*bindings.interval);
                const auto rounded = _mm_cvtss_si32(_mm_set_ss(quotient));
                cutoff = add(add(truncate(shot_tick), 1),
                             add(rounded, float(rounded) < quotient ? 1 : 0));
                has_cutoff = cutoff > initial_tick;
            }
        }
    }
    auto receive_base = timeline.last_update_tick;
    std::uint32_t pattern_index = 0;
    auto step = pattern[0];
    auto schedule = add(receive_base, step);
    while (schedule <= now) {
        if (schedule == now) {
            const auto index =
                engine::invoke<std::int32_t>(static_cast<std::byte*>(player) + 8, 10);
            if (engine::invoke<std::int32_t>(bindings.engine, 12) < index)
                break;
        }
        if (has_cutoff && cutoff > initial_tick && schedule > cutoff && add(schedule, ticks) >= now)
            break;
        const Motion batch_start = motion;
        for (std::int32_t i = 0; i < step; i = add(i, 1)) {
            const Motion before = motion;
            const float speed = std::sqrt(motion.velocity[0] * motion.velocity[0] +
                                          motion.velocity[1] * motion.velocity[1]);
            motion.velocity[0] = speed * motion.cosine;
            motion.velocity[1] = speed * motion.sine;
            if (bindings.variables.stamina_recovery && motion.stamina > 0.0f) {
                motion.stamina =
                    motion.stamina -
                    variable_float(bindings.variables.stamina_recovery) * (*bindings.interval);
                if (motion.stamina < 0.0f)
                    motion.stamina = 0.0f;
            }
            const float half_gravity =
                (variable_float(bindings.variables.gravity) * (*bindings.interval)) * 0.5f;
            motion.velocity[2] = motion.velocity[2] - half_gravity;
            if (motion.flags & 1u) {
                if (variable_integer(bindings.variables.bunnyhop) == 0) {
                    const float maximum =
                        read<float>(player, bindings.fields.maximum_speed_3eb71843) * 1.1f;
                    if (maximum > 0.0f) {
                        const float length = std::sqrt((motion.velocity[2] * motion.velocity[2] +
                                                        motion.velocity[0] * motion.velocity[0]) +
                                                       motion.velocity[1] * motion.velocity[1]);
                        if (maximum < length) {
                            const float scale = maximum / length;
                            motion.velocity[2] *= scale;
                            motion.velocity[0] *= scale;
                            motion.velocity[1] *= scale;
                        }
                    }
                }
                const float old_z = motion.velocity[2];
                const float impulse = variable_float(bindings.variables.jump_impulse);
                float jumping = (motion.flags & 2u) ? impulse : old_z + impulse;
                if (motion.stamina > 0.0f) {
                    const float fraction = stamina_clamp(1.0f - motion.stamina / 100.0f, 1.0f);
                    jumping *= fraction;
                }
                motion.velocity[2] = jumping - half_gravity;
                const float maximum = variable_float(bindings.variables.stamina_maximum);
                const float jump_scale = variable_float(bindings.variables.stamina_jump);
                motion.stamina = stamina_clamp(
                    (motion.velocity[2] - old_z) * jump_scale + motion.stamina, maximum);
            } else {
                // Air steering4333E3FF..E7BD, preserving the global alternator.
                const float ideal = 845.5f / std::sqrt(motion.velocity[0] * motion.velocity[0] +
                                                       motion.velocity[1] * motion.velocity[1]);
                if (!std::isnan(ideal)) {
                    float side_x, side_y;
                    if (motion.velocity[0] == 0.0f && motion.velocity[1] == 0.0f) {
                        side_x = 0.0f;
                        side_y = -1.0f;
                    } else {
                        const float x = motion.velocity[1] - motion.velocity[2] * 0.0f;
                        const float y = motion.velocity[2] * 0.0f - motion.velocity[0];
                        const float z = motion.velocity[0] * 0.0f - motion.velocity[1] * 0.0f;
                        const float inverse =
                            1.0f / (std::sqrt((x * x + y * y) + z * z) + 0x1p-23f);
                        side_x = inverse * x;
                        side_y = inverse * y;
                        const float cx =
                            motion.velocity[2] * side_y - inverse * z * motion.velocity[1];
                        const float cy =
                            inverse * z * motion.velocity[0] - motion.velocity[2] * side_x;
                        const float cz = side_x * motion.velocity[1] - side_y * motion.velocity[0];
                        [[maybe_unused]] volatile float magnitude =
                            std::sqrt((cx * cx + cy * cy) + cz * cz);
                    }
                    const float side_inverse =
                        1.0f /
                        (std::sqrt((side_x * side_x + side_y * side_y) + 0.0f * 0.0f) + 0x1p-23f);
                    float amount;
                    if (!(absolute(turn) > ideal) || !(30.0f > absolute(turn))) {
                        if (!bindings.alternate_strafe) {
                            motion.heading = ideal + motion.heading;
                            amount = 450.0f;
                        } else {
                            motion.heading = motion.heading - ideal;
                            amount = -450.0f;
                        }
                    } else
                        amount = turn > 0.0f ? -450.0f : 450.0f;
                    bindings.alternate_strafe = bindings.alternate_strafe == 0;
                    const float x = amount * (side_x * side_inverse),
                                y = amount * (side_y * side_inverse);
                    float wish_speed = std::sqrt((x * x + y * y) + 0.0f * 0.0f);
                    const float inverse = 1.0f / (wish_speed + 0x1p-23f);
                    const Vector direction{inverse * x, inverse * y, inverse * 0.0f};
                    const float maximum =
                        read<float>(player, bindings.fields.maximum_speed_3eb71843);
                    if (wish_speed != 0.0f && maximum <= wish_speed && wish_speed != maximum)
                        wish_speed = maximum;
                    const float acceleration = variable_float(bindings.variables.air_acceleration);
                    const float additional =
                        minimum_number(30.0f, wish_speed) -
                        ((direction[1] * motion.velocity[1] + direction[0] * motion.velocity[0]) +
                         direction[2] * motion.velocity[2]);
                    if (!(0.0f >= additional)) {
                        const float delta = minimum_number(additional, (wish_speed * acceleration) *
                                                                           (*bindings.interval));
                        motion.velocity[0] = direction[0] * delta + motion.velocity[0];
                        motion.velocity[1] = direction[1] * delta + motion.velocity[1];
                        motion.velocity[2] = direction[2] * delta + motion.velocity[2];
                    }
                }
            }
            const MovementVector minimum{-16.0f, -16.0f, 0.0f};
            const MovementVector maximum{16.0f, 16.0f, (motion.flags & 2u) ? 54.0f : 72.0f};
            MovementCollisionTrace trace{};
            trace.fraction = 1.0f;
            trace.entity = 0;
            trace.hitgroup = -1;
            trace.hitbox = -1;
            trace.displacement_flags = 0;
            auto trace_to = [&](const Vector& start, const Vector& end) {
                const auto ray =
                    make_movement_hull_ray(vector(start), vector(end), minimum, maximum);
                trace_movement_world(bindings.collision, ray, 1u, trace,
                                     bindings.engine_trace_flag);
            };
            trace_to(motion.origin, advance(motion.origin, motion.velocity, *bindings.interval));
            if (trace.fraction != 1.0f) {
                for (int bump = 0; bump < 2; ++bump) {
                    const auto normal = vector(trace.plane_normal);
                    float dot = (normal[1] * motion.velocity[1] + normal[0] * motion.velocity[0]) +
                                normal[2] * motion.velocity[2];
                    for (unsigned axis = 0; axis < 3; ++axis)
                        motion.velocity[axis] = motion.velocity[axis] - normal[axis] * dot;
                    dot = (normal[1] * motion.velocity[1] + normal[0] * motion.velocity[0]) +
                          normal[2] * motion.velocity[2];
                    if (dot < 0.0f)
                        for (unsigned axis = 0; axis < 3; ++axis)
                            motion.velocity[axis] = motion.velocity[axis] - normal[axis] * dot;
                    const float remaining = (1.0f - trace.fraction) * (*bindings.interval);
                    const auto start = vector(trace.end);
                    trace_to(start, advance(start, motion.velocity, remaining));
                    if (trace.fraction == 1.0f)
                        break;
                }
            }
            motion.origin = vector(trace.end);
            auto floor_end = motion.origin;
            floor_end[2] = floor_end[2] - 2.0f;
            trace_to(motion.origin, floor_end);
            if (trace.fraction != 1.0f && trace.plane_normal.z >= 0.7f)
                motion.flags |= 1u;
            else
                motion.flags &= 0xfffffffeu;
            motion.velocity[2] = motion.velocity[2] - half_gravity;
            const float falling = negate(motion.velocity[2]);
            if (motion.flags & 1u) {
                motion.velocity[2] = 0.0f;
                if (falling > 0.0f) {
                    const float maximum_stamina =
                        variable_float(bindings.variables.stamina_maximum);
                    const float land = variable_float(bindings.variables.stamina_land);
                    motion.stamina =
                        stamina_clamp(falling * land + motion.stamina, maximum_stamina);
                }
            }
            if ((before.flags & 1u) && !(motion.flags & 1u)) {
                write(scratch, 0x121, std::uint8_t(1));
                auto* const layers = read<void*>(player, bindings.offsets.animation_layers);
                write(layers, 0x10c, std::uint32_t(0));
                write(layers, 0x100, std::uint32_t(0));
                if (owner_mode && (motion.flags & 6u) == 6u)
                    motion.origin[2] = motion.origin[2] + 9.0f;
            }
            if (turn != 0.0f)
                motion.heading = normalize_movement_angle(motion.heading + turn);

            const float saved_time = *bindings.current_time;
            *bindings.current_time = float(motion.tick) * (*bindings.interval);
            const auto velocity = read<Vector>(player, bindings.fields.velocity_36375c23);
            write(player, bindings.fields.velocity_36375c23, motion.velocity);
            const auto origin4e = read<Vector>(player, bindings.fields.origin_4e66bb34);
            write(player, bindings.fields.origin_4e66bb34, motion.origin);
            const auto origin2a = read<Vector>(player, bindings.fields.origin_2a671b42);
            write(player, bindings.fields.origin_2a671b42, motion.origin);
            const auto tick = read<std::int32_t>(player, bindings.fields.tick_3d10b94c);
            write(player, bindings.fields.tick_3d10b94c, motion.tick);
            const auto flags = read<std::uint32_t>(player, bindings.fields.player_flags_e2fbe695);
            write(player, bindings.fields.player_flags_e2fbe695, motion.flags);
            auto angles = read<RageVector>(player, bindings.fields.angles_e23a9be1);
            if (i != sub(step, 1))
                angles[1] = newest.animation_yaw;
            const auto guard_offset = bindings.offsets.transform_guard_49d93e62 - 0x14u;
            const auto guard = read<std::uint32_t>(player, guard_offset);
            write(player, guard_offset, UINT32_MAX);
            animation::update_animation_state(scratch, angles,
                                              {*bindings.animation_frame, *bindings.current_time,
                                               *bindings.interval, bindings.animation_reset},
                                              bindings.animation_engine);
            write(player, guard_offset, guard);
            write(player, bindings.fields.player_flags_e2fbe695, flags);
            motion.tick = add(motion.tick, 1);
            write(player, bindings.fields.tick_3d10b94c, tick);
            write(player, bindings.fields.origin_2a671b42, origin2a);
            write(player, bindings.fields.origin_4e66bb34, origin4e);
            write(player, bindings.fields.velocity_36375c23, velocity);
            *bindings.current_time = saved_time;
            const float sine_term = turn_sine * motion.cosine;
            motion.cosine = turn_cosine * motion.cosine - turn_sine * motion.sine;
            motion.sine = turn_cosine * motion.sine + sine_term;
        }
        if (!(squared_distance(batch_start.origin, motion.origin) < 4096.0f) ||
            add(schedule, ticks) < now) {
            receive_base = add(receive_base, sub(motion.tick, batch_start.tick));
            ++pattern_index;
            step = pattern[pattern_index % pattern_count];
            schedule = add(receive_base, step);
        } else {
            motion = batch_start;
            break;
        }
    }
    const float factor = animation_speed_factor(scratch);
    std::int32_t result = 0;
    if ((motion.tick > initial_tick || (owner.native.flags & 0x100u)) &&
        !(squared_distance(destination.header.origin, motion.origin) < 4096.0f)) {
        destination.header.origin = motion.origin;
        destination.header.simulation_tick = motion.tick;
        destination.header.collision_max_z = (motion.flags & 2u) ? 54.0f : 72.0f;
        destination.header.velocity = motion.velocity;
        destination.header.player_flags = motion.flags;
        destination.header.animation_yaw = engine::read_opaque_native_float(scratch + 0x80);
        destination.header.animation_speed_factor = factor;
        animation::capture_entity_animation(destination.animation, player, bindings.offsets);
        destination.header.flags &= 0xffffff7bu;
        result = sub(motion.tick, initial_tick);
    }
    if (bindings.set_absolute_angles)
        bindings.set_absolute_angles(player, saved_angles);
    write(player, bindings.offsets.animation_state, original_state);
    animation::release_animation_state_vector(scratch, bindings.allocator);
    return result;
}
} // namespace skeet::lagcomp
