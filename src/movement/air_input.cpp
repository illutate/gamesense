#include "skeet/movement/air_input.hpp"
#include "movement_math.hpp"

namespace skeet {

bool prepare_air_strafe_heading(const AirStrafeSelection& selection, MovementVector velocity,
                                bool normalized_basis, UserCommand& command,
                                MovementAngleView movement_angles) noexcept {
    if (!selection.continue_solver)
        return false;
    const float forward = command.forward_move;
    const float side = command.side_move;
    command.forward_move = 0.0f;
    const float speed = movement_detail::horizontal_length(velocity);
    if (speed == 0.0f && movement_detail::absolute(velocity.z) == 0.0f)
        return false;
    const bool movement_keys = (selection.movement_key_mode & 0xffu) != 0;
    if (selection.view_angles && !movement_keys && !selection.pre_speed_requested &&
        ((command.buttons & back_button) || side != 0.0f))
        return false;
    if (normalized_basis)
        command.side_move = 0.0f;
    if (movement_keys) {
        if (normalized_basis)
            project_movement_heading(forward, side, movement_angles);
        else
            project_raw_movement_heading(forward, side, movement_angles);
    }
    return true;
}

} // namespace skeet
