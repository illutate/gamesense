#include "skeet/movement/speed_control.hpp"
#include "skeet/movement/acceleration.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

// 0x43340ACB..0x433410AC. Both braking and acceleration feed the same
// acceleration-capacity limiter; braking is not an early-return shortcut.
void regulate_movement_speed(const MovementPhysics& physics, UserCommand& command,
                             MovementVector velocity, MovementAngles command_angles,
                             MovementAngleView movement_angles, float target_speed,
                             bool clear_speed_on_acceleration) noexcept {
    if ((command.buttons & jump_button) || command_length(command) < 38.0f)
        return;
    const float speed = horizontal_length(velocity);
    if (speed < 28.0f)
        return;
    const float after_friction = friction_speed(physics, speed);
    const float fraction = after_friction / speed;
    const MovementVector friction_velocity{velocity.x * fraction, velocity.y * fraction,
                                           fraction * 0.0f};
    float difference = target_speed - after_friction;
    if (absolute(difference) < 0.5f) {
        command.forward_move = 0.0f;
        command.side_move = 0.0f;
        return;
    }
    MovementVector direction;
    if (difference < 0.0f) {
        direction = normalize_horizontal({negate(velocity.x), negate(velocity.y), 0.0f});
        movement_angles = horizontal_angles(direction, true);
        command.side_move = 0.0f;
        command.forward_move = 450.0f;
        difference = negate(difference);
    } else {
        if (clear_speed_on_acceleration)
            command.buttons &= ~speed_button;
        MovementVector forward, right;
        command_basis(command_angles, forward, right);
        if (forward.z != 0.0f)
            forward = normalize_horizontal(forward);
        if (right.z != 0.0f)
            right = normalize_horizontal(right);
        direction = normalize_horizontal(
            {command.forward_move * forward.x + command.side_move * right.x,
             command.side_move * right.y + command.forward_move * forward.y, 0.0f});
    }
    const auto capacity =
        ground_acceleration_capacity(physics, command, friction_velocity, direction);
    const float current = dot(friction_velocity, direction);
    float available =
        minimum_number(physics.player_speed_limit, physics.player_max_speed) - current;
    if (0.0f >= available)
        available = 0.0f;
    else
        available = sse_min(available, capacity.amount);
    if (!(available > difference + 0.5f))
        return;
    float desired = difference + current;
    if (capacity.ducking && physics.duck_amount == 1.0f)
        desired /= 0.34f;
    const float scale = desired / (command_length(command) + epsilon);
    command.forward_move *= scale;
    command.side_move *= scale;
    command.up_move *= scale;
}

// 0x43388405..0x43388706. The final comparison uses the acceleration amount
// returned through the helper's output parameter, not a simulated speed norm.
bool write_counter_movement(const MovementPhysics& physics, UserCommand& command,
                            MovementVector velocity, MovementAngleView movement_angles) noexcept {
    command.buttons &= ~0x61au;
    command.forward_move = 0.0f;
    command.side_move = 0.0f;
    const float speed = horizontal_length(velocity);
    if (!(speed >= 0.1f))
        return false;
    const float after_friction = friction_speed(physics, speed);
    if (!(after_friction >= 0.1f))
        return false;
    const float fraction = after_friction / speed;
    const MovementVector friction_velocity{velocity.x * fraction, velocity.y * fraction,
                                           fraction * 0.0f};
    const MovementVector reverse{negate(velocity.x), negate(velocity.y), 0.0f};
    movement_angles = horizontal_angles(reverse, false);
    command.forward_move = 450.0f;
    const MovementVector direction = normalize_horizontal(reverse);
    const float wish_speed = minimum_number(physics.player_speed_limit, physics.player_max_speed);
    const auto acceleration =
        ground_acceleration(physics, command, friction_velocity, direction, wish_speed);
    if (acceleration.amount > after_friction)
        command.forward_move = 0.001f;
    return true;
}
} // namespace skeet
