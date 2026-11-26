#include "skeet/movement/quick_stop.hpp"

namespace skeet {

QuickStopContinuation process_quick_stop_stage(const QuickStopSettings& settings, InputBitmap input,
                                               const MovementPhysics& physics, bool has_weapon,
                                               std::uint32_t& scheduler_flags, UserCommand& command,
                                               MovementVector velocity,
                                               MovementAngleView movement_angles) {
    if (!has_weapon)
        return QuickStopContinuation::player_movement;
    if (!settings.standalone_quick_stop &&
        !(settings.quick_peek_enabled && hotkey_active(settings.quick_peek_hotkey, input)))
        return QuickStopContinuation::speed_features;

    const float squared_speed =
        (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
    const float squared_input =
        command.forward_move * command.forward_move + command.side_move * command.side_move;
    if (!(squared_speed > 4.0f) || !(0.01f > squared_input) || (command.buttons & 0x61au))
        return QuickStopContinuation::speed_features;

    if (physics.player_flags & on_ground) {
        if (write_counter_movement(physics, command, velocity, movement_angles))
            scheduler_flags |= 1u;
    }
    return QuickStopContinuation::complete;
}

} // namespace skeet
