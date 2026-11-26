#include "skeet/anti_aim/yaw.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>

namespace skeet::anti_aim {
namespace {
float clamp_native(float value, float lower, float upper) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(lower)), _mm_set_ss(upper)));
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float normalize(float angle) noexcept {
    if (std::isnan(angle))
        return 0.0f;
    const auto absolute = std::bit_cast<float>(std::bit_cast<std::uint32_t>(angle) & 0x7FFFFFFFu);
    if (absolute > 180.0f) {
        const auto quotient =
            std::bit_cast<float>(std::bit_cast<std::uint32_t>(angle / 360.0f) & 0x7FFFFFFFu);
        const auto revolutions = round_movement_step(quotient) * 360.0f;
        if (angle < 0.0f)
            angle += revolutions;
        else
            angle -= revolutions;
    }
    return angle;
}
}
float transform_yaw(float base, float offset, std::int32_t mode, const float& saved_angle,
                    const YawLimits& limits, const YawFrame& frame, const YawTarget& target,
                    const EntityLookup& entities) {
    switch (mode) {
    case 0:
        return base;
    case 1:
        return base - (limits.backward_angle - offset);
    case 2: {
        const auto fraction =
            clamp_native((frame.interpolation_158 - -300.0f) / 600.0f, 0.0f, 1.0f);
        const auto initial = base - limits.maximum_angle;
        const auto interpolated = initial - limits.backward_angle * fraction;
        return interpolated + offset;
    }
    case 3:
        return saved_angle;
    case 4:
        if (target.handle == -1 || !entities.by_handle(entities.object, target.handle))
            return base - (limits.backward_angle - offset);
        {
            const auto direction = planar_movement_yaw(
                {target.x - frame.origin_x_dc, target.y - frame.origin_y_e0, 0.0f});
            const auto delta = normalize(direction - frame.angle_b8);
            const auto correction =
                clamp_native(delta * 10.0f, negate(limits.maximum_angle), limits.maximum_angle);
            return correction + (base - limits.backward_angle);
        }
    case 5:
        return offset;
    default:
        return base;
    }
}
bool lag_step_pending(std::uint32_t flags, std::int32_t limit_fc,
                      std::int32_t current_15c) noexcept {
    if ((flags & 0x80u) && limit_fc)
        limit_fc = std::bit_cast<std::int32_t>(std::uint32_t(limit_fc) - 1u);
    return current_15c < limit_fc;
}
bool apply_target_yaw_base(std::int32_t yaw_base, const YawTarget& target, float origin_x,
                           float origin_y, const EntityLookup& entities, float& yaw) {
    if (yaw_base != 1 || target.handle == -1 || !entities.by_handle(entities.object, target.handle))
        return false;
    auto quantized = [](float value) {
        const auto scaled = _mm_div_ss(_mm_set_ss(value), _mm_set_ss(0.25f));
        const auto integer = _mm_cvtss_si32(scaled);
        return _mm_cvtss_f32(
            _mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(), integer), _mm_set_ss(0.25f)));
    };
    const auto x = quantized(target.x) - quantized(origin_x);
    const auto y = quantized(target.y) - quantized(origin_y);
    yaw = planar_movement_yaw({x, y, 0.0f});
    return true;
}
} // namespace skeet::anti_aim
