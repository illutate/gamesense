#include "skeet/movement/air_steering.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

void write_air_strafe_step(const AirSteeringInput& input, AirSteeringState& state,
                           UserCommand& command, MovementAngleView movement_angles) noexcept {
    state.feedback_heading = normalize_movement_angle(movement_angles.yaw);
    const float delta = normalize_movement_angle(movement_angles.yaw - input.velocity_yaw);
    const auto smoothing =
        std::bit_cast<std::int32_t>(100u - static_cast<std::uint32_t>(input.smoothing));
    float correction = static_cast<float>(smoothing) * 0.02f;
    correction *= input.ideal_step + input.ideal_step;
    if ((input.large_turn && absolute(delta) > 170.0f && input.speed > 80.0f) ||
        (delta > correction && input.speed > 80.0f)) {
        movement_angles.yaw = input.velocity_yaw + correction;
        command.side_move = negate(input.side_magnitude);
    } else if (negate(correction) > delta && input.speed > 80.0f) {
        movement_angles.yaw = input.velocity_yaw - correction;
        command.side_move = input.side_magnitude;
    } else if (state.side_flip) {
        movement_angles.yaw -= input.ideal_step;
        command.side_move = negate(input.side_magnitude);
    } else {
        movement_angles.yaw += input.ideal_step;
        command.side_move = input.side_magnitude;
    }
    state.side_flip = !state.side_flip;
    normalize_movement_angles(movement_angles);
}

} // namespace skeet
