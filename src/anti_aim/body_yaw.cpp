#include "skeet/anti_aim/body_yaw.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>

namespace skeet::anti_aim {
namespace {
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float clamp_unit(float value) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_setzero_ps()), _mm_set_ss(1.0f)));
}
float clamp(float value, float lower, float upper) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(lower)), _mm_set_ss(upper)));
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
float normalized(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (absolute(value) > 180.0f) {
        const auto correction = round_movement_step(absolute(value / 360.0f)) * 360.0f;
        if (value < 0.0f)
            value += correction;
        else
            value -= correction;
    }
    return value;
}
float angle_difference(float destination, float source) noexcept {
    auto delta = static_cast<float>(std::fmod(static_cast<double>(destination - source), 360.0));
    if (destination > source) {
        if (delta >= 180.0f)
            delta -= 360.0f;
    } else if (delta <= -180.0f)
        delta += 360.0f;
    return delta;
}
}
void animation_yaw_limits(const AnimationYawInput& input, float& minimum, float& maximum) noexcept {
    auto modifier = (input.speed_fraction_11c * std::bit_cast<float>(0xBE99999Au) -
                     std::bit_cast<float>(0x3E4CCCCDu)) *
                        clamp_unit(input.running_fraction_f8) +
                    1.0f;
    if (input.duck_amount_a4 > 0.0f) {
        const auto blend = input.duck_amount_a4 * clamp_unit(input.ducking_fraction_fc);
        modifier += blend * (0.5f - modifier);
    }
    maximum = input.maximum_yaw_338 * modifier;
    minimum = input.minimum_yaw_334 * modifier;
}
float regular_body_yaw(const BodyYawSettings& settings, BodyYawState& state,
                       std::int32_t frame_command, std::int32_t last_command, float base_yaw,
                       const JitterRandom& random) {
    const bool opposite =
        (last_command == frame_command && (settings.mode != 3 || settings.static_offset_9c != 0)) ||
        settings.mode == 1;
    if (opposite) {
        auto delta = static_cast<float>(
            std::fmod(static_cast<double>(base_yaw - state.reference_yaw_258), 360.0));
        if (base_yaw > state.reference_yaw_258) {
            if (delta >= 180.0f)
                delta -= 360.0f;
        } else if (delta <= -180.0f)
            delta += 360.0f;
        const auto absolute =
            std::bit_cast<float>(std::bit_cast<std::uint32_t>(delta) & 0x7FFFFFFFu);
        if (absolute > 5.0f)
            state.cached_opposite_1a0 =
                delta > 0.0f ? negate(settings.opposite_angle_134) : settings.opposite_angle_134;
        return state.cached_opposite_1a0;
    }
    if (settings.mode == 3)
        return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), settings.static_offset_9c));
    if (settings.jitter_mode == 4)
        return settings.skitter[state.phase_190 % 12u] * settings.opposite_angle_134;
    float first, second;
    if (settings.jitter_seed_98 == -180) {
        first = negate(settings.jitter_maximum_144);
        second = 0.0f;
    } else if (settings.jitter_seed_98 == 0) {
        first = negate(settings.jitter_maximum_144);
        second = settings.jitter_maximum_144;
    } else if (settings.jitter_seed_98 == 180) {
        first = 0.0f;
        second = settings.jitter_maximum_144;
    } else {
        random.seed(random.context, settings.jitter_seed_98);
        first = random.range(random.context, negate(settings.jitter_maximum_144), 0.0f);
        second = random.range(random.context, 0.0f, settings.jitter_maximum_144);
    }
    return state.phase_190 & 1u ? second : first;
}
void blend_body_yaw(const BodyBlendSettings& settings, const BodyBlendInput& input,
                    float& command_yaw, std::uint8_t& send_packet) noexcept {
    const auto desired = clamp(input.offset, input.minimum_yaw, input.maximum_yaw);
    const auto goal = input.base_yaw - desired;
    auto delta = angle_difference(goal, input.feet_yaw);
    if (_mm_cvttss_si32(_mm_set_ss(input.offset)) != 0) {
        const auto direction = normalized(input.base_yaw - input.feet_yaw);
        if (!send_packet && input.choked_commands < input.choke_limit) {
            const auto amplitude = clamp(absolute(input.offset), settings.minimum_magnitude_134,
                                         settings.maximum_magnitude_138);
            auto expanded = settings.scale_154 * input.maximum_yaw + settings.expanded_angle_144;
            if (settings.minimum_magnitude_134 == settings.maximum_magnitude_138) {
                if (amplitude < settings.maximum_magnitude_138)
                    expanded = settings.base_angle_13c;
            } else
                expanded = ((expanded - settings.base_angle_13c) *
                            (amplitude - settings.minimum_magnitude_134)) /
                               (settings.maximum_magnitude_138 - settings.minimum_magnitude_134) +
                           settings.base_angle_13c;
            if (input.offset > 0.0f) {
                command_yaw = input.base_yaw - settings.base_angle_13c;
                if (direction > 0.0f)
                    command_yaw = input.base_yaw - expanded;
            } else {
                command_yaw = input.base_yaw + settings.base_angle_13c;
                if (direction < 0.0f)
                    command_yaw = expanded + input.base_yaw;
            }
        } else if (input.offset > 0.0f) {
            if (direction < 0.0f && input.choked_commands < input.choke_limit) {
                command_yaw = input.base_yaw - settings.base_angle_13c;
                send_packet = 0;
            }
        } else if (direction > 0.0f && input.choked_commands < input.choke_limit) {
            command_yaw = settings.base_angle_13c + input.base_yaw;
            send_packet = 0;
        }
        return;
    }
    if (!(absolute(delta) >= 5.0f) || input.choked_commands >= input.choke_limit) {
        if (!send_packet)
            command_yaw = goal;
        return;
    }
    send_packet = 0;
    if (delta > 0.0f)
        delta += input.maximum_yaw;
    else
        delta -= input.maximum_yaw;
    if (absolute(delta) <= settings.maximum_magnitude_138)
        command_yaw = input.feet_yaw + delta;
    else {
        const auto width = settings.expanded_angle_144 - input.maximum_yaw;
        const auto plus = width + input.feet_yaw;
        const auto minus = input.feet_yaw - width;
        const auto positive =
            clamp(angle_difference(input.base_yaw, plus), input.minimum_yaw, input.maximum_yaw);
        const auto negative =
            clamp(angle_difference(input.base_yaw, minus), input.minimum_yaw, input.maximum_yaw);
        if (absolute(positive - desired) > absolute(negative - desired))
            command_yaw = input.feet_yaw - settings.expanded_angle_144;
        else
            command_yaw = input.feet_yaw + settings.expanded_angle_144;
    }
}
template <class Special>
float solve_body(const BodyYawSettings& settings, BodyYawState& state,
                 const BodyYawRequest& request, const Special& special,
                 const JitterRandom& random) {
    float result = 0.0f;
    if (request.freestanding_body_yaw && request.last_command != request.frame_command &&
        solve_covered_body_freestanding(special.settings, request.frame_origin_dc, special.context,
                                        special.bones, request.command_pitch, request.base_yaw,
                                        request.maximum_yaw, result, special.math, special.clipper,
                                        special.workspace))
        return result;
    if (request.freestanding_body_yaw && request.last_command == request.frame_command &&
        request.shot_command == request.frame_command &&
        solve_animation_body_freestanding(request.frame_origin_dc, special.context, special.bones,
                                          request.maximum_yaw, result, special.math,
                                          special.workspace))
        return result;
    return regular_body_yaw(settings, state, request.frame_command, request.last_command,
                            request.base_yaw, random);
}
float solve_body_yaw(const BodyYawSettings& settings, BodyYawState& state,
                     const BodyYawRequest& request, const BodyYawFreestandingBindings& special,
                     const JitterRandom& random) {
    return solve_body(settings, state, request, special, random);
}
float solve_body_yaw(const BodyYawSettings& settings, BodyYawState& state,
                     const BodyYawRequest& request, const LiveBodyYawFreestandingBindings& special,
                     const JitterRandom& random) {
    return solve_body(settings, state, request, special, random);
}
} // namespace skeet::anti_aim
