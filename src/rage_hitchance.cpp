#include "skeet/rage_hitchance.hpp"
#include "rage_math.hpp"
#include <cmath>

namespace skeet {
namespace {
RageRay sample_ray(const RageHitchanceInput& input, const RageSpreadSample& sample) noexcept {
    RageVector direction;
    for (unsigned i = 0; i < 3; ++i)
        direction[i] = (sample.first * input.basis.first[i] + input.basis.center[i]) +
                       sample.second * input.basis.second[i];
    const float length = std::sqrt((direction[0] * direction[0] + direction[1] * direction[1]) +
                                   direction[2] * direction[2]);
    const float scale = input.distance / (length + 0x1p-23f);
    for (auto& component : direction)
        component *= scale;
    return {input.origin, direction};
}
}

RageHitchance evaluate_rage_hitchance(const RageHitchanceInput& input,
                                      const std::array<RageSpreadSample, 32>& samples,
                                      const RageBoxClipper& box_clipper) {
    constexpr std::array<std::uint32_t, 15> hitbox_order{0,  2, 3, 4, 6,  15, 16, 17,
                                                         18, 7, 8, 9, 10, 11, 12};
    constexpr std::array<std::uint32_t, 16> hitgroup_rank{5, 0, 2, 1, 4, 4, 6, 6,
                                                          3, 5, 5, 5, 5, 5, 5, 5};
    std::uint32_t hits = 0;
    for (const auto& sample : samples) {
        const auto ray = sample_ray(input, sample);
        RageTrace trace;
        float nearest_fraction = 1.0f;
        std::uint32_t rank_limit = 0x7fffffffU;
        const RageScanHitbox* selected = nullptr;
        for (const auto index : hitbox_order) {
            if (index >= input.hitboxes.size())
                break;
            const auto& hitbox = input.hitboxes[index];
            if (hitgroup_rank[std::uint32_t(hitbox.hitgroup) & 15U] > rank_limit)
                break;
            trace.fraction = 1.0f;
            const int clipped = hitbox.capsule.radius > 0.0f
                                    ? clip_rage_ray_to_capsule(ray, hitbox.capsule, trace)
                                    : box_clipper.clip(box_clipper.context, ray, hitbox, trace);
            if (clipped < 0 || !(nearest_fraction > trace.fraction))
                continue;
            nearest_fraction = trace.fraction;
            selected = &hitbox;
            if (hitbox.hitgroup != 1)
                break;
            rank_limit = 2;
        }
        if (!selected)
            continue;
        const auto group = selected->hitgroup;
        if (input.record_count <= 1 && input.preferred_hitgroup != group &&
            !(rage_detail::hitgroup_multipliers[std::uint32_t(group) & 15U] >=
              input.minimum_hitgroup_multiplier))
            continue;
        ++hits;
    }
    return {hits, float(hits) * 0.03125f};
}
} // namespace skeet
