#include "skeet/rage/scanning_geometry.hpp"

namespace skeet::rage {
ScanIntersection intersect_scan_hitboxes(const RageRay& ray,
                                         std::span<const RageScanHitbox> hitboxes,
                                         const RageBoxClipper& clipper) {
    constexpr std::uint32_t order[]{0, 2, 3, 4, 6, 15, 16, 17, 18, 7, 8, 9, 10, 11, 12};
    constexpr std::uint32_t rank[]{5, 0, 2, 1, 4, 4, 6, 6, 3, 5, 5, 5, 5, 5, 5, 5};
    ScanIntersection result;
    RageTrace trace;
    std::uint32_t limit = 0x7fffffffu;
    for (const auto index : order) {
        if (index >= hitboxes.size())
            break;
        const auto& hitbox = hitboxes[index];
        if (rank[static_cast<std::uint32_t>(hitbox.hitgroup) & 15u] > limit)
            break;
        trace.fraction = 1.0f;
        const int hit = hitbox.capsule.radius > 0.0f
                            ? clip_rage_ray_to_capsule(ray, hitbox.capsule, trace)
                            : clipper.clip(clipper.context, ray, hitbox, trace);
        if (hit < 0 || !(result.fraction > trace.fraction))
            continue;
        result = {index, trace.fraction, true};
        if (hitbox.hitgroup != 1)
            break;
        limit = 2;
    }
    return result;
}
}
