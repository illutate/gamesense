#include "skeet/legit/runtime.hpp"
#include <cmath>
#include <stdexcept>

namespace skeet::legit {
namespace {
float normalize_pitch(float angle) noexcept {
    if (std::isnan(angle))
        return 0.0f;
    if (std::fabs(angle) > 180.0f) {
        const auto turns = std::round(std::fabs(angle / 360.0f));
        const auto correction = turns * 360.0f;
        angle = angle < 0.0f ? angle + correction : angle - correction;
    }
    return angle;
}
}
Vector calculate_aim_step(const ProfileState& settings, const AimStepInput& input,
                          RandomBindings random) {
    auto pitch = normalize_pitch((input.yaw_delta * 0.15f) * input.curve + input.pitch_delta);
    auto yaw = input.yaw_delta;
    auto speed = settings.speed;
    if (speed == 0.0f)
        return {};
    const auto length = std::sqrt(pitch * pitch + yaw * yaw);
    if (0.022f > length)
        return {};
    const auto scale = settings.speed_scale_fov;
    if (1.0f > scale) {
        const auto fraction = 1.0f - input.target_fov / settings.maximum_fov;
        const auto inverse_scale = 1.0f - scale * scale;
        speed += ((fraction * speed) - speed) * inverse_scale;
    }
    if (input.target_distance > 500.0f) {
        auto fraction = (input.target_distance - 500.0f) * 0.001f;
        if (!(fraction > 0.5f))
            fraction = 0.5f;
        if (!(fraction < 1.0f))
            fraction = 1.0f;
        const auto multiplier = 1.5f - fraction;
        if (1.0f > multiplier)
            speed *= multiplier;
    }
    if (speed > 0.0f) {
        if (!random.seed || !random.uniform)
            throw std::invalid_argument(
                "Native aim step requires RandomSeed and RandomFloat bindings");
        const auto extent = minimum_number(speed, 0.1f);
        random.seed(random.context, input.command_number);
        speed += random.uniform(random.context, -extent, extent);
    }
    const auto maximum_step = input.time_scale * speed;
    if (0.022f > maximum_step)
        return {};
    if (length > maximum_step) {
        const auto fraction = maximum_step / length;
        yaw *= fraction;
        pitch *= fraction;
    }
    return {pitch, yaw, 0.0f};
}
} // namespace skeet::legit
