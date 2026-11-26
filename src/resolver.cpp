#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>

namespace skeet {

// 0x4341AD12..0x4341ADBD. Preserve the binary's ceil/difference test.
float round_half_away_from_zero(float value) noexcept {
    const bool negative = value < 0.0f;
    const float magnitude = negative ? -value : value;
    float rounded = static_cast<float>(std::ceil(static_cast<double>(magnitude)));
    if (rounded - magnitude > 0.5f)
        rounded -= 1.0f;
    return negative ? -rounded : rounded;
}

// Complete function 0x433AB612..0x433AB7BE, with helper calls recovered in C++.
void select_resolver_variant(ResolverRecord& record, std::uint8_t attempt) noexcept {
    if (!(record.state_flags & 0x80) || record.variant_mode == 0 || record.variant_mode > 3)
        return;

    const auto cycle = attempt % 3;
    const ResolverVariant* variant;
    if (record.variant_mode < 3) {
        // Native table 0x4347223C contains {0,3,3,3}.
        variant = cycle == 0 ? &record.center : cycle == 1 ? &record.third : &record.first;
    } else {
        float delta = record.observed_yaw - record.reference_yaw;
        if (std::isnan(delta))
            delta = 0.0f;
        else if (std::fabs(delta) > 180.0f) {
            const float turns = round_half_away_from_zero(std::fabs(delta / 360.0f));
            const float correction = turns * 360.0f;
            delta = delta < 0.0f ? delta + correction : delta - correction;
        }
        const bool positive = delta > 0.0f;
        if (cycle == 0)
            variant = positive ? &record.first : &record.third;
        else if (cycle == 1)
            variant = positive ? &record.third : &record.first;
        else
            variant = &record.center;
    }

    float pose = (variant->pose_yaw - (-60.0f)) / 120.0f;
    pose += 0.0f;
    // MAXSS/MINSS select the second operand on unordered comparisons.
    pose = pose > 0.0f ? pose : 0.0f;
    pose = pose < 1.0f ? pose : 1.0f;
    record.selected_pose_parameter = pose;
    record.selected_yaw = variant->yaw;
    copy_hitbox_matrices(variant->matrices, record.selected_matrices);
    record.selected_attempt = attempt;
}

} // namespace skeet
