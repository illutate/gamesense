#include "skeet/movement/air_feedback.hpp"
#include "movement_math.hpp"
#include <limits>

namespace skeet {
using namespace movement_detail;

void apply_guarded_pre_speed_heading(float speed, float velocity_yaw, float previous_heading,
                                     float maximum_angle, MovementAngleView angles) noexcept {
    if (!(speed > 80.0f))
        return;
    if (previous_heading != std::numeric_limits<float>::max())
        angles.yaw = previous_heading;
    const float difference = velocity_yaw - angles.yaw;
    if (std::isnan(difference) || absolute(normalize_movement_angle(difference)) < 5.0f)
        angles.yaw += maximum_angle;
}

bool resolve_pre_speed_feedback(float previous_heading, float ideal_step, float turn_limit,
                                float side_magnitude, MovementFeedback& feedback,
                                UserCommand& command, ConstMovementAngleView angles) noexcept {
    if (previous_heading == std::numeric_limits<float>::max())
        return false;
    const float difference = normalize_movement_angle(angles.yaw - previous_heading);
    const float magnitude = absolute(difference);
    if (!(magnitude > ideal_step && magnitude <= turn_limit))
        return false;
    feedback.primary_heading = normalize_movement_angle(angles.yaw);
    command.side_move = difference > 0.0f ? negate(side_magnitude) : side_magnitude;
    return true;
}

} // namespace skeet
