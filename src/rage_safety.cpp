#include "skeet/rage_safety.hpp"
#include "rage_math.hpp"

namespace skeet {
bool rage_point_safe(const RageRay& ray, std::int32_t requested_hitgroup, float minimum_multiplier,
                     const RageResolverHitboxes& hypotheses, const RageBoxClipper& clipper) {
    if (requested_hitgroup != 1) {
        const RageSegment shot{ray.start,
                               {ray.delta[0] + ray.start[0], ray.delta[1] + ray.start[1],
                                ray.delta[2] + ray.start[2]}};
        for (const auto& hitboxes : hypotheses) {
            bool hit = false;
            for (const auto& hitbox : hitboxes) {
                if (!(hitbox.capsule.radius > 0.0f))
                    continue;
                const float multiplier =
                    rage_detail::hitgroup_multipliers[std::uint32_t(hitbox.hitgroup) & 15U];
                if (!(multiplier >= minimum_multiplier))
                    continue;
                const RageSegment axis{hitbox.capsule.first, hitbox.capsule.second};
                if (hitbox.capsule.radius >= rage_segment_distance(axis, shot)) {
                    hit = true;
                    break;
                }
            }
            if (!hit)
                return false;
        }
        return true;
    }
    for (const auto& hitboxes : hypotheses) {
        RageTrace trace;
        if (clipper.clip(clipper.context, ray, hitboxes.front(), trace) < 0)
            return false;
        const float head_fraction = trace.fraction;
        for (const auto& hitbox : hitboxes) {
            if (!(hitbox.capsule.radius > 0.0f) || (hitbox.hitgroup != 2 && hitbox.hitgroup != 3))
                continue;
            trace.fraction = 1.0f;
            if (clipper.clip(clipper.context, ray, hitbox, trace) >= 0 &&
                head_fraction > trace.fraction)
                return false;
        }
    }
    return true;
}
} // namespace skeet
