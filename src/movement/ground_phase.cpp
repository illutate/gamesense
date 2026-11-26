#include "skeet/movement/ground_phase.hpp"

namespace skeet {

GroundPhaseContinuation
process_ground_phase(const GroundPhaseSettings& settings, InputBitmap input,
                     const MovementPhysics& physics, bool has_weapon, std::int32_t weapon_state,
                     bool weapon_blocked, bool& crouch_cycle_active, std::uint32_t& scheduler_flags,
                     std::int32_t command_number, UserCommand& command, MovementVector velocity,
                     MovementAngles command_angles, MovementAngleView movement_angles) {
    if (!(physics.player_flags & on_ground) || (command.buttons & jump_button)) {
        crouch_cycle_active = false;
        return GroundPhaseContinuation::player_movement;
    }
    const auto stop = process_quick_stop_stage(settings.quick_stop, input, physics, has_weapon,
                                               scheduler_flags, command, velocity, movement_angles);
    if (stop == QuickStopContinuation::player_movement)
        return GroundPhaseContinuation::player_movement;
    if (stop == QuickStopContinuation::complete)
        return GroundPhaseContinuation::complete;
    if (process_slow_motion_stage(settings.slow_motion, input, physics, weapon_state,
                                  weapon_blocked, scheduler_flags, command_number, command,
                                  velocity, command_angles, movement_angles))
        return GroundPhaseContinuation::complete;
    return GroundPhaseContinuation::weapon_speed_features;
}

} // namespace skeet
