#include "skeet/rage/preamble_tail.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include "skeet/movement/extrapolation.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <immintrin.h>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class T> void store(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
constexpr float degrees = 57.2957763671875f;
std::uint32_t command_hash(const client::Command& command) noexcept {
    std::uint32_t result = 0x811C9DC5;
    for (const auto byte : std::as_bytes(std::span(&command, 1)))
        result = (std::to_integer<std::uint8_t>(byte) ^ result) * 0x1000193;
    return result ^ 0x92CA5DF1;
}
SpreadParameters spread_parameters(const client::CommandFrame& frame, bool patterns) noexcept {
    return {std::uint32_t(frame.weapon_id_d8),
            std::int32_t(frame.field_b0),
            field<float>(&frame, 0xD0),
            field<float>(&frame, 0xC4),
            std::bit_cast<float>(frame.field_100),
            field<std::int32_t>(&frame, 0x19C),
            patterns,
            frame.weapon_kind_124};
}
MovementAngles command_angles(const client::Command& command) noexcept {
    return {command.angles[0], command.angles[1], command.angles[2]};
}
void command_angles(client::Command& command, const MovementAngles& angles) noexcept {
    command.angles = {angles.pitch, angles.yaw, angles.roll};
}
void subtract_recoil(client::Command& command, client::CommandFrame& frame, bool normalize) {
    const auto recoil = field<RageVector>(&frame, 0x9C);
    if ((recoil[0] * recoil[0] + recoil[1] * recoil[1]) + recoil[2] * recoil[2] != 0.0f) {
        for (unsigned i = 0; i < 3; ++i)
            command.angles[i] -= recoil[i];
        if (normalize) {
            auto angles = command_angles(command);
            normalize_movement_angles(angles);
            command_angles(command, angles);
        }
        frame.flags |= 0x10U;
    }
}
MovementAngles direction_angles(RageVector direction, const TargetAngleMath& math) {
    const auto squared =
        (direction[0] * direction[0] + direction[1] * direction[1]) + direction[2] * direction[2];
    const float inverse = 1.0f / (std::sqrt(squared) + 0x1p-23f);
    for (auto& component : direction)
        component *= inverse;
    if (direction[0] == 0.0f && direction[1] == 0.0f)
        return {direction[2] > 0.0f ? 270.0f : 90.0f, 0, 0};
    const auto yaw = float(math.atan2(double(direction[1]), double(direction[0]))) * degrees;
    const auto horizontal = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    return {float(math.atan2(double(-direction[2]), double(horizontal))) * degrees, yaw, 0};
}
void remove_spread_vector(client::Command& command, RageSpreadSample spread,
                          const TargetAngleMath& math) {
    MovementVector forward, right, up;
    movement_angle_vectors(command_angles(command), &forward, &right, &up);
    RageVector direction{forward.x - (up.x * spread.second + right.x * spread.first),
                         forward.y - (up.y * spread.second + right.y * spread.first),
                         forward.z - (up.z * spread.second + right.z * spread.first)};
    auto angles = direction_angles(direction, math);
    normalize_movement_angles(angles);
    command_angles(command, angles);
}
void limit_step(client::Command& command, const client::CommandFrame& frame, void* player,
                const PreambleTailBindings& b) {
    auto reference = field<MovementAngles>(&frame, 0x20);
    if (!(field<std::uint32_t>(player, b.player_offsets.flags) & 8U) &&
        field<std::uint8_t>(player, b.player_offsets.first_byte) ==
            field<std::uint8_t>(player, b.player_offsets.second_byte)) {
        const auto number = std::bit_cast<std::int32_t>(std::uint32_t(command.number) - 1U);
        if (const auto* previous = engine::invoke<const client::Command*>(b.input, 8, 0, number))
            reference = command_angles(*previous);
    }
    auto angles = command_angles(command);
    limit_firing_angle_step(angles, reference, field<float>(&frame, 0x1E8));
    command_angles(command, angles);
}
// The air-side correction433578B3..43357E25. The entry pitch/roll changes
// precede these gates and remain observable when the prediction path is skipped.
bool correct_air_origin(PreambleState& state, PreambleResult& result, void* player,
                        const PreambleTailBindings& b) {
    auto& frame = *result.frame;
    auto& command = *result.command;
    if (!player || !b.shared.settings.enabled.value || (state.flags & 1U) ||
        (frame.flags & 0x40U) || command.movement.side_move == 0.0f || (frame.flags & 0x40000U) ||
        command.angles[2] == 0.0f)
        return false;
    command.movement.side_move = 0.0f;
    if (!(frame.flags & 0x1000U))
        return false;
    MovementSimulation simulation{};
    const auto simulate = [&](const MovementCaptureInput& capture,
                              const MovementEnvironmentBindings& environment,
                              MovementCollisionEngine& collision) {
        initialize_live_movement_simulation(
            simulation, player, field<MovementVector>(&frame, 0x168),
            field<MovementVector>(&frame, 0x180), field<std::uint32_t>(&frame, 0xF8),
            field<std::uint32_t>(&frame, 0xC8), capture);
        simulate_live_movement_tick(simulation, collision, environment, player, command);
    };
    if (const auto* decoded =
            std::get_if<std::reference_wrapper<const anti_aim::TargetRefreshContext>>(
                &b.shared.target_refresh)) {
        const auto& context = decoded->get().scored;
        simulate(context.movement, context.movement_environment, context.common.collision);
    } else {
        simulate(b.action.live_movement.capture, b.action.live_movement.environment,
                 b.action.collision);
    }
    const auto delta_x = simulation.position.x - frame.origin_10c[0];
    const auto delta_y = simulation.position.y - frame.origin_10c[1];
    const auto squared = delta_y * delta_y + delta_x * delta_x;
    if (!(squared > 0.01f && 4096.0f > squared))
        return false;
    frame.origin_10c[0] = simulation.position.x;
    frame.origin_10c[1] = simulation.position.y;
    frame.origin_dc[0] += delta_x;
    frame.origin_dc[1] += delta_y;
    const auto point = field<RageVector>(&frame, 0xE8);
    const auto recoil = field<RageVector>(&frame, 0x9C);
    const bool category_recoil = legit::recoil_compensation_enabled(
        b.legit_settings, std::uint32_t(std::int32_t(std::int8_t(frame.weapon_kind_124))),
        b.shared.input);
    auto angles = target_aim_angles(point, frame.origin_dc, recoil,
                                    category_recoil || !b.shared.settings.remove_recoil.value,
                                    frame.flags, b.angle_math);
    command_angles(command, angles);
    if (frame.flags & 0x40U)
        limit_step(command, frame, player, b);
    return true;
}
bool shot_path(const client::CommandFrame& frame, const client::Command& command) noexcept {
    if (frame.flags & 0x100000U)
        return true;
    if ((frame.flags & 0x400U) && (command.movement.buttons & 1U))
        return true;
    return (frame.flags & 2U) && (command.movement.buttons & 0x800U) &&
           (frame.weapon_id_d8 == 64 || frame.weapon_kind_124 == 0);
}
void correct_shot_angles(PreambleState& state, PreambleResult& result, void* player,
                         const PreambleTailBindings& b) {
    auto& frame = *result.frame;
    auto& command = *result.command;
    const auto& shared = b.shared;
    const auto parameters = spread_parameters(frame, shared.shotgun_patterns);
    const auto seed = std::uint32_t(command.random_seed) & 0xFFU;
    if (frame.flags & 0x4000U) {
        const auto spread =
            generate_spread_sample(parameters, seed, shared.spread_tables, shared.random,
                                   shared.pattern, shared.trigonometry);
        const float radius = std::sqrt(spread.first * spread.first + spread.second * spread.second);
        if (!(std::numeric_limits<float>::min() > radius)) {
            if (frame.flags & 8U) {
                const auto pitch = b.arctangent(radius) * degrees;
                command.angles[0] += pitch;
                command.angles[2] =
                    -(float(b.angle_math.atan2(double(spread.first), double(spread.second))) *
                      degrees);
                if (correct_air_origin(state, result, player, b)) {
                    command.angles[0] += b.arctangent(radius) * degrees;
                    command.angles[2] =
                        -(float(b.angle_math.atan2(double(spread.first), double(spread.second))) *
                          degrees);
                }
            } else
                remove_spread_vector(command, spread, b.angle_math);
        }
    }
    if (!shared.settings.remove_recoil.value)
        return;
    if (legit::recoil_compensation_enabled(
            b.legit_settings, std::uint32_t(std::int32_t(std::int8_t(frame.weapon_kind_124))),
            shared.input))
        return;
    if (shared.settings.pattern_recoil_correction.value && shared.shotgun_patterns &&
        shared.pattern.get && parameters.pellet_count > 1) {
        const auto spread = preamble_pattern_correction(parameters, seed, shared.random,
                                                        shared.pattern, shared.trigonometry);
        const auto radius = std::sqrt(spread.first * spread.first + spread.second * spread.second);
        if (!(std::numeric_limits<float>::min() > radius))
            remove_spread_vector(command, spread, b.angle_math);
    }
    subtract_recoil(command, frame, true);
}
bool publish_historical_command(PreambleResult& result, client::CommandFrame& original,
                                const PreambleTailBindings& b) {
    auto& frame = *result.frame;
    auto& command = *result.command;
    if (&frame == &original || command_hash(command) == result.command_hash)
        return false;
    auto& verified = b.verified_commands[std::uint32_t(frame.command_number_ac) % 150U];
    if (verified.command.number != frame.command_number_ac)
        return false;
    command.movement.forward_move = field<float>(&frame, 0x78);
    command.movement.side_move = field<float>(&frame, 0x80);
    client::reorient_command_movement(command, field<std::array<float, 3>>(&frame, 0x118),
                                      frame.state_104, (frame.flags & 0x40000U) != 0, true,
                                      b.button_policy, b.command_trigonometry);
    client::sanitize_command(command, (frame.flags & 8U) != 0, b.command_environment);
    client::save_final_command(command);
    client::publish_verified_command(verified, command, b.checksum);
    client::invalidate_command_prediction(b.prediction);
    return true;
}
}
RageSpreadSample preamble_pattern_correction(const SpreadParameters& parameters, std::uint32_t seed,
                                             const SpreadRandom& random,
                                             const ShotgunPattern& pattern,
                                             const RageTrigonometry& trig) {
    if (!pattern.get || !parameters.shotgun_patterns || parameters.pellet_count <= 1)
        return {0.0f, 0.0f};
    random.seed(random.context, std::bit_cast<std::int32_t>(seed + 1U));
    const auto recoil = _mm_cvttss_si32(_mm_set_ss(parameters.recoil_index));
    const auto first = std::uint32_t(recoil) * std::uint32_t(parameters.pellet_count);
    RageSpreadSample sum{0.0f, 0.0f};
    for (std::int32_t i = 0; i < parameters.pellet_count; ++i) {
        random.uniform(random.context, 0.0f, 1.0f);
        random.uniform(random.context, 0.0f, 6.283185482025146484375f);
        float angle = 0.0f, radius = 0.0f;
        pattern.get(pattern.context, parameters.weapon_id, parameters.fire_mode,
                    std::bit_cast<std::int32_t>(first + std::uint32_t(i)), angle, radius);
        const auto sine = trig.sine(angle);
        const auto cosine = trig.cosine(angle);
        const auto value = transform_spread_radius(parameters.weapon_id, parameters.fire_mode,
                                                   parameters.recoil_index, radius) *
                           parameters.spread;
        sum.second = value * sine + sum.second;
        sum.first = cosine * value + sum.first;
    }
    const float inverse = 1.0f / float(parameters.pellet_count);
    return {inverse * sum.first, sum.second * inverse};
}
PreambleTailResult finish_rage_command(PreambleState& state, PreambleResult& result,
                                       client::CommandFrame& original, void* player,
                                       const PreambleTailBindings& b, PreambleTailEntry entry) {
    PreambleTailResult tail;
    if (entry == PreambleTailEntry::return_only)
        return tail;
    auto& frame = *result.frame;
    if (entry == PreambleTailEntry::after_firing && &frame == &original &&
        field<std::int32_t>(&frame, 0xC8) != result.tick_base && !(state.flags & 0x800U)) {
        store(&frame, 0xC8, result.tick_base);
        b.shared.prediction_time = float(result.tick_base) * b.shared.interval;
    }
    if (entry == PreambleTailEntry::after_firing || entry == PreambleTailEntry::actions) {
        update_action_state(b.action_state, frame, player, *result.command, b.action);
        if (shot_path(frame, *result.command) && frame.weapon_address) {
            const auto weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
            if (lagcomp::history_weapon_is_firearm(
                    field<std::uint16_t>(weapon, b.shared.offsets.weapon_identifier))) {
                b.shared.selected_marker = frame.command_number_ac;
                tail.shot_marker_updated = true;
                correct_shot_angles(state, result, player, b);
            }
        }
    }
    if (entry != PreambleTailEntry::prediction_clock)
        tail.historical_command_published = publish_historical_command(result, original, b);
    // The native local at stack+120 starts at FLT_MAX and is overwritten by
    // 4340310B's returned snapshot pointer, including null, on frame selection.
    if (std::bit_cast<float>(result.prediction_clock_marker) != std::numeric_limits<float>::max())
        b.shared.prediction_time = 1.0f;
    return tail;
}
} // namespace skeet::rage
