#include "skeet/legit/manual_motion.hpp"
#include <cmath>

namespace skeet::legit {
namespace {
float normalize(float angle) {
    if (std::isnan(angle))
        return 0;
    if (std::fabs(angle) > 180.0f) {
        const auto correction = std::round(std::fabs(angle / 360.0f)) * 360.0f;
        angle = angle < 0.0f ? angle + correction : angle - correction;
    }
    return angle;
}
}
void update_manual_motion(RuntimeState& state, const client::Command& command,
                          const Vector& frame_angles, float time,
                          const lagcomp::RotationMath& math) {
    const auto base = state.previous_view_valid ? state.previous_view : frame_angles;
    const auto pitch = normalize((command.angles[0] - state.remainder[0]) - base[0]);
    const auto yaw = normalize((command.angles[1] - state.remainder[1]) - base[1]);
    state.raw_motion = {state.time_scale * pitch, state.time_scale * yaw, state.time_scale * 0.0f};
    state.remainder = {};
    const auto prior_length = std::sqrt(state.filtered_motion[0] * state.filtered_motion[0] +
                                        state.filtered_motion[1] * state.filtered_motion[1]);
    if (state.raw_motion != state.filtered_motion) {
        const auto old_quaternion = lagcomp::history_angle_quaternion(state.filtered_motion, math);
        const auto new_quaternion = lagcomp::history_angle_quaternion(state.raw_motion, math);
        const auto aligned = lagcomp::align_history_quaternion(old_quaternion, new_quaternion);
        const auto interpolated = lagcomp::slerp_history_quaternion(
            old_quaternion, aligned, state.time_scale * 0.06666667f, math);
        state.filtered_motion = lagcomp::history_quaternion_angles(interpolated, math);
    }
    const auto length = std::sqrt(state.filtered_motion[0] * state.filtered_motion[0] +
                                  state.filtered_motion[1] * state.filtered_motion[1]);
    const auto moving =
        std::fabs(state.raw_motion[0]) >= 0.022f || std::fabs(state.raw_motion[1]) >= 0.022f;
    const auto accelerating = length != 0.0f && length >= prior_length;
    state.profile.secondary_flags = std::uint8_t((state.profile.secondary_flags & 0xF9u) |
                                                 (moving ? 2u : 0u) | (accelerating ? 4u : 0u));
    const auto raw_length = std::sqrt(state.raw_motion[1] * state.raw_motion[1] +
                                      state.raw_motion[0] * state.raw_motion[0]);
    if (raw_length >= 0.022f)
        state.manual_time = time;
    state.last_command = command.number;
}
} // namespace skeet::legit
