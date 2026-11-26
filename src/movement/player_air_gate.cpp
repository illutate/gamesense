#include "skeet/movement/player_air_gate.hpp"
#include "skeet/movement/acceleration.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
namespace {
using namespace movement_detail;

template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

float air_accelerate(const PlayerAirGateSettings& settings) noexcept {
    const void* node = settings.air_accelerate;
    for (;;) {
        const void* next = field<const void*>(node, 0x1Cu);
        if (node == next)
            break;
        node = next;
    }
    const auto encoded = field<std::uint32_t>(node, 0x2Cu);
    const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node));
    return std::bit_cast<float>(encoded ^ address);
}

MovementAngles counter_angles(MovementVector direction) noexcept {
    if (direction.x == 0.0f && direction.y == 0.0f)
        return {direction.z > 0.0f ? 270.0f : 90.0f, 0.0f, 0.0f};
    const auto yaw = static_cast<float>(
        std::atan2(static_cast<double>(direction.y), static_cast<double>(direction.x)));
    const float horizontal = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    const auto pitch = static_cast<float>(
        std::atan2(static_cast<double>(negate(direction.z)), static_cast<double>(horizontal)));
    return {pitch * radians_to_degrees, yaw * radians_to_degrees, 0.0f};
}
}

PlayerAirGateResult process_player_air_gate(const PlayerAirGateSettings& settings,
                                            const PlayerAirGateFrame& frame,
                                            const PlayerAirGateBindings& bindings,
                                            std::uint32_t& scheduler_flags, UserCommand& command,
                                            MovementAngleView movement_angles) {
    using Continuation = PlayerAirGateContinuation;
    PlayerAirGateResult result{Continuation::complete, false};
    const auto& fields = bindings.fields;
    auto* const player = frame.player;
    if (field<std::uint8_t>(player, fields.movement_type_7501cb91) != 2u)
        return result;

    result.air_strafe_enabled = settings.air_strafe_enabled;
    result.continuation = Continuation::air_solver;
    // 433BA215 returns the unsigned byte comparison >=2.
    if (field<std::uint8_t>(player, fields.water_state_ac7f2913) >= 2u)
        return result;

    const auto player_flags =
        field<std::uint8_t>(player, bindings.environment.fields.player_flags_e2fbe695);
    auto* const weapon = frame.weapon;
    if (player_flags & on_ground) {
        if ((scheduler_flags & 8u) && weapon &&
            field<std::uint16_t>(weapon, fields.weapon_definition_5c9f0fbb) == 31u &&
            (command.buttons & jump_button)) {
            scheduler_flags &= ~8u;
            command.buttons &= ~jump_button;
            result.continuation = Continuation::complete;
        }
        return result;
    }

    if (settings.bunny_hop_enabled)
        suppress_airborne_jump(true, player_flags, bindings.bunny_hop_guard, command);

    if (!(scheduler_flags & 8u) || !(*bindings.speed_policy_flags & 0x80u) ||
        !(frame.feedback->original_buttons & speed_button) || !weapon ||
        field<std::uint16_t>(weapon, fields.weapon_definition_5c9f0fbb) != 40u ||
        field<std::uint8_t>(weapon, fields.weapon_blocked_b8969256) != 0u)
        return result;
    const auto vertical = field<float>(player, fields.velocity_36375c23 + 8u);
    if (-80.0f > vertical)
        return result;
    const auto weapon_attack = field<float>(weapon, fields.weapon_next_attack_f71ce331);
    const auto player_attack = field<float>(player, fields.player_next_attack_de530a8b);
    if (maximum_number(player_attack, weapon_attack) > *bindings.current_time)
        return result;

    command.buttons &= ~0x618u;
    command.forward_move = 0.0f;
    command.side_move = 0.0f;
    result.continuation = Continuation::ground_speed_tail;

    const auto velocity = field<MovementVector>(player, fields.velocity_36375c23);
    const float speed = std::sqrt((velocity.x * velocity.x + velocity.y * velocity.y) + 0.0f);
    const float reciprocal = 1.0f / (speed + epsilon);
    MovementVector direction{velocity.x * reciprocal, velocity.y * reciprocal, 0.0f * reciprocal};

    const float first_acceleration = air_accelerate(settings);
    const float reduction = minimum_number(settings.brake_reduction_limit,
                                           *bindings.environment.interval * first_acceleration);
    const auto physics = capture_movement_command_physics(player, bindings.environment);
    const float threshold = maximum_number(
        settings.brake_threshold,
        minimum_number(physics.player_speed_limit, physics.player_max_speed) * 0.34f - reduction);
    // COMISS speed,threshold / JB also exits for unordered values.
    if (!(speed >= threshold))
        return result;

    direction = {negate(direction.x), negate(direction.y), negate(direction.z)};
    movement_angles = counter_angles(direction);
    command.forward_move = settings.maximum_move;
    const float second_acceleration = air_accelerate(settings);
    const float bounded_acceleration =
        minimum_number(settings.brake_threshold, second_acceleration);
    if (!(bounded_acceleration > 0.0f))
        return result;

    command.forward_move = sse_min(settings.maximum_move,
                                   speed / (*bindings.environment.interval * bounded_acceleration));
    const float probe_acceleration = air_accelerate(settings);
    const auto probe_velocity = field<MovementVector>(player, fields.velocity_36375c23);
    const float amount = air_acceleration_delta(probe_velocity, direction, speed,
                                                probe_acceleration, *bindings.environment.interval);
    const auto current_velocity = field<MovementVector>(player, fields.velocity_36375c23);
    const float next_y = direction.y * amount + current_velocity.y;
    const float next_x = direction.x * amount + current_velocity.x;
    if (std::sqrt(next_y * next_y + next_x * next_x) > speed)
        command.forward_move = 0.001f;
    return result;
}

} // namespace skeet
