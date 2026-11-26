#include "skeet/movement/steering_math.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

float round_movement_step(float value) noexcept {
    const bool negative = !(value >= 0.0f);
    float rounded =
        static_cast<float>(std::ceil(static_cast<double>(negative ? negate(value) : value)));
    const float gap = negative ? rounded + value : rounded - value;
    if (gap > 0.5f)
        rounded -= 1.0f;
    return negative ? negate(rounded) : rounded;
}

float planar_movement_yaw(MovementVector direction) noexcept {
    if (direction.x == 0.0f && direction.y == 0.0f)
        return 0.0f;
    const float radians = static_cast<float>(
        std::atan2(static_cast<double>(direction.y), static_cast<double>(direction.x)));
    return radians * radians_to_degrees;
}

void movement_angle_vectors(MovementAngles angles, MovementVector* forward, MovementVector* right,
                            MovementVector* up) noexcept {
    const float pitch = angles.pitch * degrees_to_radians;
    const float yaw = angles.yaw * degrees_to_radians;
    const float roll = angles.roll * degrees_to_radians;
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const float sr = std::sin(roll), cr = std::cos(roll);
    if (forward) {
        forward->x = cy * cp;
        forward->y = sy * cp;
        forward->z = negate(sp);
    }
    if (right) {
        const float minus_cr = cr * -1.0f;
        const float sr_sp = sr * sp;
        right->y = minus_cr * cy - sr_sp * sy;
        right->x = minus_cr * negate(sy) - sr_sp * cy;
        right->z = (sr * -1.0f) * cp;
    }
    if (up) {
        const float cr_sp = cr * sp;
        up->z = cr * cp;
        up->x = cr_sp * cy - negate(sy) * sr;
        up->y = cr_sp * sy - sr * cy;
    }
}

void project_movement_heading(float forward_move, float side_move,
                              MovementAngleView movement_angles) noexcept {
    MovementVector forward, right;
    movement_angle_vectors(movement_angles, &forward, &right, nullptr);
    if (forward.z != 0.0f)
        forward = normalize_horizontal(forward);
    if (right.z != 0.0f)
        right = normalize_horizontal(right);
    const MovementVector desired{side_move * right.x + forward_move * forward.x,
                                 side_move * right.y + forward_move * forward.y, 0.0f};
    movement_angles.yaw = planar_movement_yaw(normalize_horizontal(desired));
}

void project_raw_movement_heading(float forward_move, float side_move,
                                  MovementAngleView movement_angles) noexcept {
    MovementVector forward, right;
    movement_angle_vectors(movement_angles, &forward, &right, nullptr);
    movement_angles.yaw =
        planar_movement_yaw({side_move * right.x + forward.x * forward_move,
                             side_move * right.y + forward.y * forward_move, 0.0f});
}

float normalize_movement_angle(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (absolute(value) > 180.0f) {
        const float turns = round_movement_step(absolute(value / 360.0f)) * 360.0f;
        if (value < 0.0f)
            value += turns;
        else
            value -= turns;
    }
    return value;
}

void normalize_movement_angles(MovementAngleView angles) noexcept {
    angles.pitch = normalize_movement_angle(angles.pitch);
    angles.yaw = normalize_movement_angle(angles.yaw);
    angles.roll = normalize_movement_angle(angles.roll);
}

} // namespace skeet
