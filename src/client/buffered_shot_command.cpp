#include "skeet/client/buffered_shot_command.hpp"
#include "skeet/rage/buffered_shot_repair.hpp"
#include "skeet/rage/command_phase6.hpp"
#include "skeet/rage/feedback_owner.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <xmmintrin.h>

namespace skeet::client {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t integer(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return integer(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
std::int32_t ticks(float time, float interval) noexcept {
    return truncate(time / interval + 0.5f);
}
float convar_float(void* variable) noexcept {
    return std::bit_cast<float>(lagcomp::read_prediction_convar(variable));
}
std::uint32_t convar_integer(void* variable) noexcept {
    for (;;) {
        auto* parent = read<void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return read<std::uint32_t>(variable, 0x30) ^
           std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
}
float interpolation_time(const BufferedShotCommandBindings& b) noexcept {
    const auto& c = b.convars;
    const auto rate = convar_float(c.interpolation_rate);
    const auto minimum = convar_float(c.interpolation_minimum);
    const auto ratio = convar_float(c.interpolation_ratio);
    return maximum_number(b.interpolation_floor, maximum_number(minimum, ratio / rate));
}
float squared_distance(const RageVector& first, const RageVector& second) noexcept {
    const float y = first[1] - second[1], x = first[0] - second[0], z = first[2] - second[2];
    return (y * y + x * x) + z * z;
}
bool needs_repair(const rage::ShotDiagnosticEntry& entry, const CommandFrame& current, void* player,
                  const BufferedShotCommandBindings& b) {
    if (entry.local_tick != ticks(read<float>(player, b.offsets.simulation_time), b.interval)) {
        const auto number = engine::invoke<std::int32_t>(b.engine_client, 205);
        const auto* history =
            prediction::CommandHistoryView::from_owner(b.history_owner).find(number);
        if (history && (history->flags & 2u))
            return true;
    }
    if (std::bit_cast<float>(entry.local_field_7f5ed2d0) >
        read<float>(player, b.offsets.local_field_7f5ed2d0))
        return true;

    // The actual engine slot returns the +8 networkable subobject.
    const auto* networkable =
        engine::invoke<const std::byte*>(b.entity_list, 1, entry.target_handle);
    if (!networkable)
        return true;
    const auto* target = networkable - 8;
    if (read<std::uint8_t>(target, b.offsets.target_rejection))
        return true;
    const auto index =
        entry.target_handle == UINT32_MAX ? 0x1FFFu : std::uint16_t(entry.target_handle);
    const auto& cached = *b.players.get(index);
    if (integer(entry.target_field_168) < read<std::int32_t>(&cached.native, 0x168))
        return false;
    if (cached.history.count >= 2 && squared_distance(cached.history.at(0).header.origin,
                                                      cached.history.at(1).header.origin) > 4096.0f)
        return true;
    if ((read<std::uint32_t>(target, b.offsets.target_flags) ^ entry.target_flags) & 1u)
        return true;
    if (!(cached.native.cached_target_height == entry.target_height))
        return true;
    if (!(read<float>(target, b.offsets.target_duck) == entry.target_duck))
        return true;
    if (!(cached.native.reference_yaw == entry.target_reference_yaw))
        return true;

    // 4337F00E's intentional seconds truncation, repeated inline here.
    const auto base = integer(current.field_18c + current.field_d4);
    const auto seconds = truncate(float(base) * b.interval - float(b.maximum_unlag_ms) * 0.001f);
    const auto corrected = ticks(float(seconds), b.interval);
    return add(entry.target_tick, ticks(interpolation_time(b), b.interval)) < corrected;
}

void refresh_prediction_frame(CommandFrame& frame, std::int32_t number,
                              const BufferedShotCommandBindings& b) noexcept {
    const auto* history = prediction::CommandHistoryView::from_owner(b.history_owner).find(number);
    if (!history)
        return;
    const bool preserve_height = frame.view_height_84 == frame.height_164;
    const auto origin = read<RageVector>(history, 0x2C);
    const RageVector delta{origin[0] - frame.origin_10c[0], origin[1] - frame.origin_10c[1],
                           origin[2] - frame.origin_10c[2]};
    const float square = (delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2];
    if (square > 0.010000000707805157f) {
        for (std::size_t axis = 0; axis < 3; ++axis)
            frame.origin_dc[axis] += delta[axis];
        frame.origin_10c = origin;
    }
    const auto height = read<float>(history, 0x28);
    if (preserve_height) {
        const float difference = height - frame.height_164;
        if (difference * difference > 0.010000000707805157f) {
            frame.height_164 = frame.view_height_84 = height;
            frame.origin_dc[2] = origin[2] + height;
        }
    }
    const auto scale = convar_float(b.convars.recoil_scale);
    const auto& raw = history->recoil_angles;
    const RageVector recoil{raw[0] * scale, raw[1] * scale, raw[2] * scale};
    if (squared_distance(recoil, frame.recoil_angles_9c) > 0.010000000707805157f)
        frame.recoil_angles_9c = recoil;
    const auto latency = network::average_latency_ticks(b.latency);
    write(&frame, 0x14C, latency);
    const auto& timing = b.timing;
    const auto command_tick = network::adjusted_command_tick(
        read<std::int32_t>(timing.command_clock, 4), timing.acknowledged != nullptr,
        timing.acknowledged ? *timing.acknowledged : 0, timing.sent != nullptr,
        timing.sent ? *timing.sent : 0);
    frame.field_d4 = std::uint32_t(latency) + std::uint32_t(command_tick);
    frame.field_18c = 0;
    write(&frame, 0x144, frame.field_d4 + convar_integer(b.convars.command_tick_offset));
}

std::array<float, 3> direction_angles(RageVector direction, const rage::TargetAngleMath& math) {
    const float square =
        (direction[0] * direction[0] + direction[1] * direction[1]) + direction[2] * direction[2];
    const float inverse = 1.0f / (std::sqrt(square) + 0x1p-23f);
    for (auto& component : direction)
        component *= inverse;
    if (direction[0] == 0.0f && direction[1] == 0.0f)
        return {direction[2] > 0.0f ? 270.0f : 90.0f, 0.0f, 0.0f};
    constexpr float degrees = 57.2957763671875f;
    const auto yaw = float(math.atan2(double(direction[1]), double(direction[0]))) * degrees;
    const auto horizontal = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    return {float(math.atan2(double(-direction[2]), double(horizontal))) * degrees, yaw, 0.0f};
}
void normalize(std::array<float, 3>& angles) noexcept {
    normalize_movement_angles(angles);
}
void correct_spread(Command& command, const CommandFrame& frame,
                    const BufferedShotCommandBindings& b) {
    if (!(frame.flags & 0x4000u))
        return;
    const rage::SpreadParameters parameters{std::uint32_t(frame.weapon_id_d8),
                                            integer(frame.field_b0),
                                            frame.spread_d0,
                                            frame.inaccuracy_c4,
                                            frame.weapon_field_100,
                                            frame.pellet_count_19c,
                                            convar_integer(b.convars.shotgun_patterns) != 0,
                                            frame.weapon_kind_124};
    const auto spread =
        rage::generate_spread_sample(parameters, std::uint8_t(command.random_seed), b.spread_tables,
                                     b.random, b.shotgun_pattern, b.spread_trigonometry);
    const float radius = std::sqrt(spread.first * spread.first + spread.second * spread.second);
    if (std::numeric_limits<float>::min() > radius)
        return;
    constexpr float degrees = 57.2957763671875f;
    if (frame.flags & 8u) {
        command.angles[0] += b.arctangent(radius) * degrees;
        command.angles[2] =
            -(float(b.angle_math.atan2(double(spread.first), double(spread.second))) * degrees);
        return;
    }
    MovementVector forward, right, up;
    movement_angle_vectors({command.angles[0], command.angles[1], command.angles[2]}, &forward,
                           &right, &up);
    const RageVector direction{forward.x - (up.x * spread.second + right.x * spread.first),
                               forward.y - (up.y * spread.second + right.y * spread.first),
                               forward.z - (up.z * spread.second + right.z * spread.first)};
    command.angles = direction_angles(direction, b.angle_math);
    normalize(command.angles);
}
void refresh_pending_shot(const CommandFrame& frame, const lagcomp::EvaluationState& selected,
                          const BufferedShotCommandBindings& b) {
    auto* feedback = rage::feedback_owner_state(b.pending_shots.active);
    if (!feedback || feedback->pending.empty())
        return;
    auto& shot = feedback->pending.back();
    if (shot.words[1] != std::uint32_t(frame.command_number_ac))
        return;
    rage::shot_capture_target(shot, selected, frame.origin_dc, 1, b.pending_shots);
}
struct PredictionTimeRestore {
    float& time;
    float saved;
    explicit PredictionTimeRestore(float& value) noexcept : time(value), saved(value) {}
    ~PredictionTimeRestore() {
        time = saved;
    }
};
}

BufferedShotCommandStorage::BufferedShotCommandStorage() noexcept {
    diagnostics.count = 0;
    for (auto& entry : diagnostics.entries)
        entry.target_handle = UINT32_MAX;
    for (auto& frame : frames)
        initialize_command_frame(frame);
}
void prepare_buffered_shot_commands(BufferedShotCommandOwner& owner) noexcept {
    if (auto* source = owner.live()) {
        source->prepare(owner.state);
        return;
    }
    if (owner.decoded()->choked == 0)
        owner.state.diagnostics.count = 0;
}
void repair_buffered_shot_commands(BufferedShotCommandOwner& owner, CommandFrame& current,
                                   void* player, std::uint8_t send_packet) {
    if (auto* source = owner.live()) {
        source->repair(owner.state, current, player, send_packet);
        return;
    }
    const auto& b = *owner.decoded();
    auto& slot = owner.state.frames[std::uint32_t(current.command_number_ac) % 150u];
    if (b.enabled && current.field_1a0 == 0)
        std::memcpy(&slot, &current, sizeof current);
    else
        slot.command_number_ac = -1;
    auto& diagnostics = owner.state.diagnostics;
    if (!b.checksum || diagnostics.count == 0 || !b.enabled || (current.flags & 0x40u) ||
        !b.firing_mode_enabled || command_roll_blocked(b.command_environment) ||
        current.choked_15c <= 0 || (!send_packet && current.choked_15c < current.choke_limit_fc))
        return;

    const auto first = add(b.last_outgoing, 1);
    const auto number = current.command_number_ac;
    const auto count = diagnostics.count;
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
        auto& entry = diagnostics.entries[ordinal];
        const auto age_bits =
            std::bit_cast<std::uint32_t>(b.clock_time - std::bit_cast<float>(entry.clock));
        if (std::bit_cast<float>(age_bits & 0x7FFFFFFFu) > 1.0f)
            break;
        if (entry.command < first || entry.command >= number ||
            !needs_repair(entry, current, player, b))
            continue;
        const auto index = std::uint32_t(entry.command) % 150u;
        auto& raw = b.commands[index];
        auto& verified = b.verified[index];
        if (raw.number != entry.command || verified.command.number != entry.command ||
            !(verified.command.movement.buttons & 1u))
            continue;
        Command command = verified.command;
        auto& frame = owner.state.frames[index];
        if (frame.command_number_ac != entry.command || frame.weapon_id_d8 == 64 ||
            frame.weapon_id_d8 != current.weapon_id_d8 ||
            frame.weapon_address != current.weapon_address)
            continue;

        PredictionTimeRestore restore(b.prediction_time);
        b.prediction_time = float(frame.tick_base_c8) * b.interval;
        refresh_prediction_frame(frame, entry.command, b);
        rage::BufferedShotRepairScratch scratch;
        const auto* selected =
            rage::repair_buffered_shot(frame, player, command, entry, scratch, b.repair);
        if (!selected)
            continue;
        refresh_pending_shot(frame, *selected, b);
        const RageVector point{std::bit_cast<float>(selected->words[3]),
                               std::bit_cast<float>(selected->words[4]),
                               std::bit_cast<float>(selected->words[5])};
        command.angles =
            direction_angles({point[0] - frame.origin_dc[0], point[1] - frame.origin_dc[1],
                              point[2] - frame.origin_dc[2]},
                             b.angle_math);
        for (std::size_t axis = 0; axis < 3; ++axis)
            command.angles[axis] -= frame.recoil_angles_9c[axis];
        normalize(command.angles);
        correct_spread(command, frame, b);
        command.tick = add(integer(selected->words[0xB4 / 4]),
                           ticks(read<float>(&current, 0x198), b.interval));
        if ((frame.flags & 0x400008u) == 0x400008u)
            command.angles[0] = normalize_movement_angle(command.angles[0]) - 720.0f;
        command.movement.forward_move = frame.input_forward_78;
        command.movement.side_move = frame.input_side_80;
        reorient_command_movement(command, frame.angles_118, frame.state_104,
                                  (frame.flags & 0x40000u) != 0, true, b.button_policy,
                                  b.trigonometry);
        sanitize_command(command, (frame.flags & 8u) != 0, b.command_environment);
        if (b.send_mode)
            rage::publish_command_animation_angles(b.animation_angles, command.number,
                                                   command.angles);
        save_final_command(command);
        publish_verified_command(verified, command, b.checksum);
        const auto predicted = raw.predicted;
        std::memcpy(&raw, &command, sizeof command);
        raw.predicted = predicted;
    }
    diagnostics.count = 0;
}

} // namespace skeet::client
