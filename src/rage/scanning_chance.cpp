#include "skeet/rage/scanning_chance.hpp"
#include <cmath>

namespace skeet::rage {
RageHitchance evaluate_scan_safe_hitchance(const ScanSafeHitchanceInput& input,
                                           std::span<const RageSpreadSample, 32> samples,
                                           const RageBoxClipper& clipper) {
    std::uint32_t hits = 0;
    for (const auto& sample : samples) {
        RageVector direction;
        for (std::size_t axis = 0; axis != 3; ++axis)
            direction[axis] = (input.basis.first[axis] * sample.first + input.basis.center[axis]) +
                              input.basis.second[axis] * sample.second;
        const auto magnitude =
            std::sqrt((direction[0] * direction[0] + direction[1] * direction[1]) +
                      direction[2] * direction[2]);
        const auto inverse = 1.0f / (magnitude + 0x1p-23f);
        for (auto& value : direction) {
            value *= inverse;
            value *= input.range;
        }
        if (rage_point_safe({input.origin, direction}, input.hitgroup,
                            input.minimum_hitgroup_multiplier, input.hypotheses, clipper))
            ++hits;
    }
    return {hits, float(hits) * 0.03125f};
}
}
