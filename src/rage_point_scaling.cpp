#include "rage_point_math.hpp"
#include <cmath>
#include <xmmintrin.h>

namespace skeet::rage_point_detail {
namespace {
struct PointSegmentDistance {
    float distance;
    float projection;
};
// 0x4338590E. Distances beyond either endpoint are negative, while projection
// remains the signed distance along the segment's epsilon-normalized axis.
PointSegmentDistance point_segment_distance(const RageVector& point, const RageVector& from,
                                            const RageVector& to) noexcept {
    const auto delta = subtract(to, from);
    const float magnitude = length(delta);
    const auto axis = normalize(delta, magnitude);
    const auto relative = subtract(point, from);
    const float projection =
        (axis[1] * relative[1] + axis[0] * relative[0]) + axis[2] * relative[2];
    if (projection < 0.0f)
        return {-length(relative), projection};
    if (projection > magnitude)
        return {-length(subtract(point, to)), projection};
    const RageVector closest{axis[0] * projection + from[0], axis[1] * projection + from[1],
                             axis[2] * projection + from[2]};
    return {length(subtract(point, closest)), projection};
}
} // namespace

void scale_points_from(std::vector<RageAimPoint>& points, std::size_t start_index,
                       const RageVector& center, const RagePointGeneration& settings) {
    if (start_index >= points.size())
        return;
    if (settings.fixed_scale) {
        for (std::size_t i = start_index; i < points.size(); ++i) {
            auto& point = points[i];
            point.position = interpolate(center, point.position, settings.point_scale);
            point.scale = settings.point_scale;
        }
        return;
    }
    const auto delta = subtract(center, settings.origin);
    const float distance = length(delta);
    const auto axis = normalize(delta, distance);
    const float accuracy = float(settings.accuracy_percent) * 0.01f;
    const float margin = ((distance * settings.spread) * accuracy) * accuracy;
    const RageVector extended{axis[0] * 60.0f + center[0], axis[1] * 60.0f + center[1],
                              axis[2] * 60.0f + center[2]};
    for (std::size_t i = start_index; i < points.size();) {
        auto& point = points[i];
        const auto projected = point_segment_distance(point.position, settings.origin, extended);
        const float remaining = projected.distance - margin;
        if (!(remaining > 0.0f)) {
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        const float axial = std::fabs(projected.projection - distance);
        const float radius = std::sqrt(remaining * remaining + axial * axial);
        const float ratio = radius / length(subtract(center, point.position));
        const float limited = minimum_number(ratio, settings.point_scale);
        const auto low = _mm_max_ss(_mm_set_ss(limited), _mm_set_ss(0.25f));
        const float fraction = _mm_cvtss_f32(_mm_min_ss(low, _mm_set_ss(0.9f)));
        point.position = interpolate(center, point.position, fraction);
        point.scale = fraction;
        ++i;
    }
}
} // namespace skeet::rage_point_detail
