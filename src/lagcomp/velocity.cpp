#include "skeet/lagcomp/velocity.hpp"
#include <bit>
#include <cmath>

namespace skeet::lagcomp {
void adjust_capture_velocity(Vector& velocity, float blend, float maximum_speed,
                             float interpolated_factor) noexcept {
    constexpr float epsilon = 0x1p-23f;
    float limit;
    if (blend <= 0.0f) {
        limit = maximum_speed * std::bit_cast<float>(0x3F666666u);
        const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
        if (!(limit > speed))
            return;
        const float denominator =
            std::sqrt(velocity[1] * velocity[1] + velocity[0] * velocity[0]) + epsilon;
        const float scale = limit / denominator;
        velocity[0] *= scale;
        velocity[1] = scale * velocity[1];
    } else if (blend >= 1.0f) {
        limit = maximum_speed * std::bit_cast<float>(0x3F0CCCCDu);
        const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
        if (!(speed > limit))
            return;
        const float denominator =
            std::sqrt(velocity[1] * velocity[1] + velocity[0] * velocity[0]) + epsilon;
        const float scale = limit / denominator;
        velocity[0] *= scale;
        velocity[1] *= scale;
    } else {
        limit = maximum_speed * interpolated_factor;
        const float denominator =
            std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]) + epsilon;
        const float scale = limit / denominator;
        velocity[0] *= scale;
        velocity[1] *= scale;
    }
}
} // namespace skeet::lagcomp
