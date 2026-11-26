#include "skeet/movement/ground_phase_live.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/movement/speed_control_live.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
using namespace movement_detail;
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float periodic_wave(const client::Command& command, float interval, float rate,
                    float amplitude) noexcept {
    const float phase = (static_cast<float>(command.number % 1000) * interval) * rate;
    float wave = phase - static_cast<float>(std::floor(static_cast<double>(phase)));
    wave = absolute(wave - 0.5f);
    wave += wave;
    return wave * amplitude;
}
}

GroundPhaseContinuation process_live_ground_phase(MovementCommandOwner& owner, void* player,
                                                  void* weapon, client::Command& command,
                                                  MovementAngleView angles,
                                                  const MovementCommandBindings& b) {
    if (!(movement_command_player_flags(player, b) & on_ground) ||
        (command.movement.buttons & jump_button)) {
        owner.duck_peek.crouch_active = false;
        return GroundPhaseContinuation::player_movement;
    }
    if (!weapon)
        return GroundPhaseContinuation::player_movement;
    const bool quick_stop = owner.config.standalone_quick_stop.value ||
                            (owner.quick_peek_config.enabled.value &&
                             hotkey_active(owner.quick_peek_config.hotkey.value.binding, b.input));
    if (quick_stop) {
        const auto velocity = movement_command_velocity(player, b);
        const float squared =
            (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
        const float input = command.movement.forward_move * command.movement.forward_move +
                            command.movement.side_move * command.movement.side_move;
        if (squared > 4.0f && 0.01f > input && !(command.movement.buttons & 0x61au)) {
            if (movement_command_player_flags(player, b) & on_ground) {
                if (write_live_movement_counter(player, command.movement,
                                                movement_command_velocity(player, b), angles, b))
                    b.scheduler_flags |= 1u;
            }
            return GroundPhaseContinuation::complete;
        }
    }
    const bool slow =
        owner.config.slow_motion.value && hotkey_active(b.slow_motion_hotkey, b.input);
    if (!slow && !(b.scheduler_flags & 0x40u))
        return GroundPhaseContinuation::weapon_speed_features;
    if ((b.scheduler_flags & 8u) && b.weapon_state != -1 && !(b.weapon_state & 4))
        return GroundPhaseContinuation::weapon_speed_features;
    b.scheduler_flags =
        (b.scheduler_flags & 0xfffedfe7u) | ((b.scheduler_flags & 8u) << 10) | 0x80u;
    if (movement_command_weapon_blocked(weapon, b))
        return GroundPhaseContinuation::complete;
    const auto maximum = capture_movement_command_speed(player, b);
    const float ceiling =
        (minimum_number(maximum.player_speed_limit, maximum.player_max_speed) - 1.0f) * 0.34f;
    const float wave = periodic_wave(command, *b.environment.interval, 7.0f, 4.0f);
    regulate_live_movement_speed(player, command.movement, movement_command_velocity(player, b),
                                 command.angles, angles, ceiling - wave, true, b);
    return GroundPhaseContinuation::complete;
}

void process_live_ground_speed_tail(const MovementCommandOwner& owner, void* player,
                                    client::Command& command, MovementAngleView angles,
                                    const MovementCommandBindings& b) {
    if (!(movement_command_player_flags(player, b) & on_ground) ||
        field<std::uint8_t>(player, b.fields.player_state_ac7f2913) >= 2 ||
        (command.movement.buttons & 0x2002u))
        return;
    if (b.periodic_gate_4346a740 && b.periodic_gate_43467f60 && b.periodic_gate_434685b8) {
        const auto maximum = capture_movement_command_speed(player, b);
        const float ceiling =
            minimum_number(maximum.player_speed_limit, maximum.player_max_speed) - 1.0f;
        const float wave = periodic_wave(command, *b.environment.interval, 10.0f, 10.0f);
        regulate_live_movement_speed(player, command.movement, movement_command_velocity(player, b),
                                     command.angles, angles, ceiling - wave, false, b);
    }
    if (owner.config.fast_walk.value && !(command.movement.buttons & duck_button) &&
        (command.movement.buttons & speed_button))
        regulate_live_movement_speed(player, command.movement, movement_command_velocity(player, b),
                                     command.angles, angles, 134.0f, true, b);
}

} // namespace skeet
