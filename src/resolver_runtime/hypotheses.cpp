#include "skeet/resolver_runtime/hypotheses.hpp"
#include "skeet/resolver.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

namespace skeet::resolver_runtime {
namespace {
float clamp_native(float value, float low, float high) noexcept {
    value = value > low ? value : low;
    return value < high ? value : high;
}

float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (std::fabs(value) > 180.0f) {
        const float turns = round_half_away_from_zero(std::fabs(value / 360.0f));
        const float correction = turns * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value;
}

float difference(float destination, float source) noexcept {
    // The subtraction is single precision before the external _CIfmod call.
    float result = static_cast<float>(std::fmod(static_cast<double>(destination - source), 360.0));
    if (destination > source) {
        if (result >= 180.0f)
            result -= 360.0f;
    } else if (result <= -180.0f)
        result += 360.0f;
    return result;
}

float quantize(float value) noexcept {
    const float scaled = value * 182.0444488525390625f;
    // CVTTSS2SI returns the integer indefinite value for NaN/out-of-range.
    std::int32_t integer = std::numeric_limits<std::int32_t>::min();
    if (scaled >= -2147483648.0f && scaled < 2147483648.0f)
        integer = static_cast<std::int32_t>(scaled);
    return static_cast<float>(static_cast<std::uint32_t>(integer) & 0xffffU) * 0.0054931640625f;
}
}

std::array<Hypothesis, 3> solve_hypotheses(const HypothesisInput& in) noexcept {
    float modifier =
        ((0.8f - in.speed_fraction * 0.3f) - 1.0f) * clamp_native(in.running_fraction, 0.0f, 1.0f) +
        1.0f;
    if (in.duck_amount > 0.0f) {
        const float duck = in.duck_amount * clamp_native(in.ducking_fraction, 0.0f, 1.0f);
        modifier += duck * (0.5f - modifier);
    }
    const float maximum = in.maximum_yaw * modifier;
    const float minimum = in.minimum_yaw * modifier;
    constexpr float seeds[3] = {118.0f, 0.0f, -118.0f};
    std::array<Hypothesis, 3> output{};
    for (std::uint32_t index = 0; index != 3; ++index) {
        float feet = index == 1 ? in.eye_yaw : in.feet_yaw;
        float movement = in.movement_yaw;
        float target = in.movement_yaw_target;
        for (std::uint32_t iteration = 0; iteration != 2; ++iteration) {
            const float eye = iteration == 0 ? seeds[index] + in.eye_yaw : in.eye_yaw;
            const float eye_delta = difference(eye, feet);
            if (eye_delta > maximum)
                feet = eye - std::fabs(maximum);
            else if (eye_delta < minimum)
                feet = std::fabs(minimum) + eye;
            feet = normalize(feet);

            const float horizontal_speed =
                std::sqrt(in.velocity_x * in.velocity_x + in.velocity_y * in.velocity_y);
            const bool moving = horizontal_speed > 0.1f || in.velocity_z > 100.0f;
            const float step = std::fabs(moving ? (in.speed_fraction * 20.0f + 30.0f) * in.interval
                                                : in.interval * 100.0f);
            const float goal = quantize(moving ? eye : in.lower_body_yaw);
            feet = quantize(feet);
            const float delta = normalize(goal - feet);
            if (delta > step)
                feet += step;
            else if (delta < -step)
                feet -= step;
            else
                feet = goal;

            if (horizontal_speed > 0.0f && in.moving != 0) {
                double angle = std::atan2(static_cast<double>(-in.velocity_y),
                                          static_cast<double>(-in.velocity_x));
                float direction = static_cast<float>((angle * 180.0) / 3.14159265358979323846);
                if (direction < 0.0f)
                    direction += 360.0f;
                target = normalize(difference(direction, feet));
            }
            const float move_delta = normalize(difference(target, movement));
            if ((in.record_flags & 0x400U) != 0 || in.animation_fraction >= 1.0f)
                movement = target;
            else {
                const float running = clamp_native(in.running_fraction, 0.0f, 1.0f);
                const float ducking = clamp_native(in.ducking_fraction, 0.0f, 1.0f);
                const float blend = (ducking - running) * in.duck_amount + running;
                const float exponent = std::log(0.18f) * -1.4427f;
                movement = normalize((std::pow(blend, exponent) + 0.1f) * move_delta + movement);
            }
        }
        feet = normalize(feet);
        float pose = difference(in.eye_yaw, feet);
        if (pose >= 0.0f && in.maximum_yaw != 0.0f)
            pose = (pose / in.maximum_yaw) * 58.0f;
        else if (in.minimum_yaw != 0.0f)
            pose = (pose / in.minimum_yaw) * -58.0f;
        output[index] = {feet, clamp_native(pose, -60.0f, 60.0f),
                         clamp_native((pose - -60.0f) / 120.0f + 0.0f, 0.0f, 1.0f),
                         clamp_native((movement - -180.0f) / 360.0f + 0.0f, 0.0f, 1.0f)};
    }
    return output;
}

} // namespace skeet::resolver_runtime
