#include "skeet/rage_points.hpp"
#include "skeet/rage_segments.hpp"
#include "rage_point_math.hpp"
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace skeet {
namespace {
using namespace rage_point_detail;
constexpr unsigned order[]{3, 4, 0, 6, 8, 7, 10, 9, 17, 15, 18, 16, 11, 12};
PointBasis point_basis(const RageVector& center, const RageVector& origin) noexcept {
    const auto delta = subtract(center, origin);
    const auto axis = normalize(
        delta, std::sqrt((delta[2] * delta[2] + delta[0] * delta[0]) + delta[1] * delta[1]));
    return basis_from_axis(axis);
}
bool obstructs_head(const RageCapsule& body, const RageVector& origin,
                    const RageVector& point) noexcept {
    return body.radius >= rage_segment_distance({body.first, body.second}, {origin, point});
}
void capsule_points(std::vector<RageAimPoint>& points, const RageScanHitbox& hitbox,
                    const RageCapsule& body, const RageVector& origin, const PointBasis& basis) {
    const auto& capsule = hitbox.capsule;
    const auto append = [&](const RageVector& position) {
        points.push_back({position, 1.0f, hitbox.hitgroup, 2});
    };
    if (hitbox.hitgroup >= 4 && hitbox.hitgroup <= 7) {
        const auto delta = subtract(capsule.second, capsule.first);
        const auto axis = normalize(delta, length(delta));
        append({capsule.radius * axis[0] + capsule.second[0],
                capsule.radius * axis[1] + capsule.second[1],
                capsule.radius * axis[2] + capsule.second[2]});
    } else if (hitbox.hitgroup == 3) {
        append(support(capsule, basis.side, true));
        append(support(capsule, {-basis.side[0], -basis.side[1], -basis.side[2]}));
    } else {
        const RageVector directions[]{
            basis.upper, basis.side, {-basis.side[0], -basis.side[1], -basis.side[2]}};
        for (auto direction : directions) {
            auto point = support(capsule, direction);
            if (hitbox.hitgroup == 1 && obstructs_head(body, origin, point)) {
                const RageVector sum{direction[0] + basis.upper[0], direction[1] + basis.upper[1],
                                     direction[2] + basis.upper[2]};
                direction = normalize(
                    sum, std::sqrt((sum[1] * sum[1] + sum[2] * sum[2]) + sum[0] * sum[0]));
                point = support(capsule, direction);
            }
            append(point);
        }
    }
}
void box_points(std::vector<RageAimPoint>& points, const RageLocalHitbox& local,
                const RageScanHitbox& hitbox, const RageVector& origin, const PointBasis& basis) {
    const float middle_y = (local.maximum[1] + local.minimum[1]) * 0.5f;
    const RageVector corners[]{{local.minimum[0], middle_y, local.minimum[2]},
                               {local.maximum[0], middle_y, local.maximum[2]},
                               {local.maximum[0], middle_y, local.minimum[2]},
                               {local.minimum[0], middle_y, local.maximum[2]}};
    float maximum = -std::numeric_limits<float>::max(), minimum = std::numeric_limits<float>::max();
    RageVector highest, lowest;
    for (const auto& corner : corners) {
        RageVector position;
        for (unsigned row = 0; row < 3; ++row) {
            const auto& matrix = hitbox.transform.rows[row];
            position[row] =
                ((corner[1] * matrix[1] + matrix[0] * corner[0]) + matrix[2] * corner[2]) +
                matrix[3];
        }
        const auto delta = subtract(position, origin);
        const float projection =
            ((delta[0] * basis.side[0] + delta[1] * basis.side[1]) + delta[2] * basis.side[2]) /
            length(delta);
        if (projection > maximum) {
            maximum = projection;
            highest = position;
        }
        if (minimum > projection) {
            minimum = projection;
            lowest = position;
        }
    }
    if (maximum != -std::numeric_limits<float>::max())
        points.push_back({highest, 1.0f, local.hitgroup, 2});
    if (minimum != std::numeric_limits<float>::max())
        points.push_back({lowest, 1.0f, local.hitgroup, 2});
}
}

std::vector<RageAimPoint>
generate_rage_regular_points(const RagePointGeneration& settings,
                             std::span<const RageLocalHitbox, 19> local,
                             std::span<const RageScanHitbox, 19> hitboxes) {
    std::vector<RageAimPoint> result;
    for (const auto index : order) {
        const auto points = generate_rage_regular_hitbox_points(settings, index, local, hitboxes);
        result.insert(result.end(), points.begin(), points.end());
    }
    return result;
}

std::vector<RageAimPoint>
generate_rage_regular_hitbox_points(const RagePointGeneration& settings, std::uint32_t index,
                                    std::span<const RageLocalHitbox> local,
                                    std::span<const RageScanHitbox> hitboxes) {
    std::vector<RageAimPoint> result;
    if (index >= local.size() || index >= hitboxes.size())
        return result;
    const auto group_bit =
        index == 11 || index == 12 ? 0x200u : 1u << (std::uint32_t(local[index].hitgroup) & 31u);
    if (!(settings.regular_mask & group_bit))
        return result;
    const auto& hitbox = hitboxes[index];
    const auto center = midpoint(hitbox.capsule);
    RageAimPoint primary{center, 0.0f, local[index].hitgroup, 0};
    if (hitbox.hitgroup == 1) {
        auto lowered = center;
        lowered[2] -= 1.0f;
        if (obstructs_head(hitboxes[6].capsule, settings.origin, lowered))
            primary.position =
                interpolate(center, support(hitbox.capsule, {0.0f, 0.0f, 1.0f}, true), 0.6f);
    }
    std::vector<RageAimPoint> extra;
    if (settings.multipoint_mask & group_bit) {
        const auto basis = point_basis(center, settings.origin);
        if (hitbox.capsule.radius > 0.0f)
            capsule_points(extra, hitbox, hitboxes[6].capsule, settings.origin, basis);
        else
            box_points(extra, local[index], hitbox, settings.origin, basis);
        scale_points(extra, center, settings);
    }
    if (hitbox.hitgroup == 3)
        primary.position = interpolate(primary.position, midpoint(hitboxes[2].capsule), 0.5f);
    result.push_back(primary);
    result.insert(result.end(), extra.begin(), extra.end());
    return result;
}
} // namespace skeet
