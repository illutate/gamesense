#include "skeet/rage_safe_points.hpp"
#include "skeet/rage_segments.hpp"
#include "rage_safe_point_detail.hpp"
#include <cmath>

namespace skeet {
namespace {
using namespace rage_point_detail;
bool head_obstructed(const RageVector& point, const RageVector& origin,
                     const std::array<RageCapsule, 3>& heads,
                     const std::array<RageCapsule, 3>& bodies) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        RageTrace trace{1.0f, point, {}, false};
        clip_rage_ray_to_capsule({origin, subtract(point, origin)}, heads[i], trace);
        if (rage_segment_distance({bodies[i].first, bodies[i].second}, {origin, trace.point}) <=
            bodies[i].radius)
            return true;
    }
    return false;
}
bool intersects_other_heads(const RageRay& ray, unsigned selected,
                            const std::array<RageCapsule, 3>& heads) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        if (i == selected)
            continue;
        const auto& capsule = heads[i];
        const auto closest = closest_rage_ray_segment(ray, {capsule.first, capsule.second});
        const float weight = 1.0f - closest.second_fraction;
        RageVector delta;
        delta[0] = (capsule.second[0] * closest.second_fraction + weight * capsule.first[0]) -
                   (ray.delta[0] * closest.first_fraction + ray.start[0]);
        delta[1] = (capsule.second[1] * closest.second_fraction + capsule.first[1] * weight) -
                   (ray.delta[1] * closest.first_fraction + ray.start[1]);
        delta[2] = (capsule.first[2] * weight + closest.second_fraction * capsule.second[2]) -
                   (ray.delta[2] * closest.first_fraction + ray.start[2]);
        // COMISS/JB also rejects unordered distances from a zero-length axis.
        if (!(capsule.radius >= length(delta)))
            return false;
    }
    return true;
}
std::vector<RageAimPoint> search_head_points(const RageVector& origin,
                                             const SafePointBounds& bounds,
                                             const std::array<RageCapsule, 3>& bodies,
                                             std::int32_t hitgroup, const RageTrigonometry& trig) {
    const auto& heads = bounds.last_hypotheses;
    const auto& side = bounds.basis.side;
    const auto& upper = bounds.basis.upper;
    const RageVector opposite{-side[0], -side[1], -side[2]};
    const RageVector axes[]{side, opposite, upper};
    std::array<float, 3> scores{-1.0f, -1.0f, -1.0f};
    std::array<RageVector, 3> selected{};
    float angle = 0.0f;
    for (unsigned step = 0; step < 15; ++step, angle += 0.10471975803375244f) {
        const float sine = trig.sine(angle);
        const float cosine = trig.cosine(angle);
        const RageVector vertical{upper[0] * cosine, upper[1] * cosine, upper[2] * cosine};
        for (unsigned sign = 0; sign < 2; ++sign) {
            const auto& lateral = axes[sign];
            const RageVector direction{lateral[0] * sine + vertical[0],
                                       lateral[1] * sine + vertical[1],
                                       lateral[2] * sine + vertical[2]};
            const auto normalized = normalize(direction, length(direction));
            const RageVector delta{normalized[0] * -1000.0f, normalized[1] * -1000.0f,
                                   normalized[2] * -1000.0f};
            for (unsigned i = 0; i < 3; ++i) {
                const auto center = midpoint(heads[i]);
                const RageRay cross_section{{center[0] - delta[0] * 0.5f,
                                             center[1] - delta[1] * 0.5f,
                                             center[2] - delta[2] * 0.5f},
                                            delta};
                const auto intersection = intersect_rage_capsule(cross_section, heads[i]);
                const auto to_point = subtract(intersection.point, origin);
                const RageRay ray{origin, normalize(to_point, length(to_point))};
                if (!intersects_other_heads(ray, i, heads) ||
                    head_obstructed(intersection.point, origin, heads, bodies))
                    continue;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const float score =
                        (axes[axis][1] * ray.delta[1] + axes[axis][0] * ray.delta[0]) +
                        axes[axis][2] * ray.delta[2];
                    if (score > scores[axis]) {
                        scores[axis] = score;
                        selected[axis] = intersection.point;
                    }
                }
            }
        }
    }
    if (scores[0] == -1.0f || scores[1] == -1.0f || scores[2] == -1.0f)
        return {};
    return {{selected[0], 1.0f, hitgroup, 3},
            {selected[1], 1.0f, hitgroup, 3},
            {selected[2], 1.0f, hitgroup, 3}};
}
}

std::vector<RageAimPoint>
generate_rage_head_safe_points(const RagePointGeneration& settings, const RageVector& center,
                               const RageVector& target_origin, const RageLocalHitbox& head,
                               std::span<const Matrix3x4, 3> hypotheses,
                               const RageHeadOcclusion* occlusion, const RageTrigonometry& trig) {
    using namespace rage_point_detail;
    auto bounds = build_safe_point_bounds(settings.origin, center, head, hypotheses);
    if (bounds.points.empty())
        return {};
    if (occlusion) {
        std::array<RageCapsule, 3> bodies;
        for (unsigned i = 0; i < 3; ++i)
            bodies[i] = transform_capsule(occlusion->hitbox, occlusion->matrices[i]);
        // The original checks the upper bound first, then the lower bound.
        if (head_obstructed(bounds.points[1].position, settings.origin, bounds.last_hypotheses,
                            bodies) ||
            head_obstructed(bounds.points[0].position, settings.origin, bounds.last_hypotheses,
                            bodies))
            bounds.points =
                search_head_points(settings.origin, bounds, bodies, head.hitgroup, trig);
    }
    return finish_safe_points(std::move(bounds.points), settings, target_origin, 0, head.hitgroup);
}
} // namespace skeet
