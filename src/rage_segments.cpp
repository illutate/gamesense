#include "skeet/rage_segments.hpp"
#include <cmath>
#include <xmmintrin.h>

namespace skeet {
namespace {
RageVector difference(const RageVector& a, const RageVector& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float dot_xyz(const RageVector& a, const RageVector& b) noexcept {
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}
float dot_yxz(const RageVector& a, const RageVector& b) noexcept {
    return (a[1] * b[1] + a[0] * b[0]) + a[2] * b[2];
}
bool outside(float value) noexcept {
    return !(value >= 0.0f && value <= 1.0f);
}
std::array<float, 2> line_parameters(const RageVector& origin, const RageVector& direction,
                                     const RageSegment& segment) noexcept {
    const auto b = difference(segment.second, segment.first);
    const auto relative = difference(origin, segment.first);
    const float along_b = dot_yxz(b, relative);
    const float product = dot_xyz(direction, b);
    const float b_squared = dot_xyz(b, b);
    const float determinant = dot_xyz(direction, direction) * b_squared - product * product;
    float s = 0.0f;
    if (determinant != 0.0f)
        s = (product * along_b - dot_yxz(direction, relative) * b_squared) / determinant;
    return {s, (s * product + along_b) / b_squared};
}
}
RageVector project_rage_point_to_segment(const RageSegment& segment, const RageVector& point,
                                         float& fraction) noexcept {
    const auto delta = difference(segment.second, segment.first);
    const auto offset = difference(point, segment.first);
    const float projected = dot_xyz(offset, delta) / dot_xyz(delta, delta);
    const auto low = _mm_max_ss(_mm_set_ss(projected), _mm_setzero_ps());
    fraction = _mm_cvtss_f32(_mm_min_ss(low, _mm_set_ss(1.0f)));
    return {delta[0] * fraction + segment.first[0], delta[1] * fraction + segment.first[1],
            delta[2] * fraction + segment.first[2]};
}

RageSegmentClosest closest_rage_segments(const RageSegment& first,
                                         const RageSegment& second) noexcept {
    const auto a = difference(first.second, first.first);
    auto [s, t] = line_parameters(first.first, a, second);
    RageVector first_point;
    if (!outside(s)) {
        if (outside(t)) {
            const auto& endpoint = t < 0.0f ? second.first : second.second;
            t = t < 0.0f ? 0.0f : 1.0f;
            first_point = project_rage_point_to_segment(first, endpoint, s);
        } else
            first_point = {s * a[0] + first.first[0], s * a[1] + first.first[1],
                           s * a[2] + first.first[2]};
    } else if (!outside(t)) {
        first_point = s < 0.0f ? first.first : first.second;
        s = s < 0.0f ? 0.0f : 1.0f;
        project_rage_point_to_segment(second, first_point, t);
    } else {
        const auto first_endpoint = s < 0.0f ? first.first : first.second;
        const auto second_endpoint = t < 0.0f ? second.first : second.second;
        s = s < 0.0f ? 0.0f : 1.0f;
        t = t < 0.0f ? 0.0f : 1.0f;
        float first_projection, second_projection;
        const auto first_candidate =
            project_rage_point_to_segment(first, second_endpoint, first_projection);
        const auto second_candidate =
            project_rage_point_to_segment(second, first_endpoint, second_projection);
        const auto first_delta = difference(first_candidate, second_endpoint);
        const auto second_delta = difference(second_candidate, first_endpoint);
        const float first_distance = dot_yxz(first_delta, first_delta);
        const float second_distance = dot_yxz(second_delta, second_delta);
        if (second_distance >= first_distance) {
            s = first_projection;
            first_point = first_candidate;
        } else {
            t = second_projection;
            first_point = first_endpoint;
        }
    }
    return {s, t, first_point};
}

float rage_segment_distance(const RageSegment& first, const RageSegment& second) noexcept {
    const auto parameters = closest_rage_segments(first, second);
    const float first_weight = 1.0f - parameters.first_fraction;
    const float second_weight = 1.0f - parameters.second_fraction;
    RageVector delta;
    for (unsigned i = 0; i < 3; ++i) {
        const float a = first.first[i] * first_weight + first.second[i] * parameters.first_fraction;
        const float b =
            i == 0
                ? second.second[i] * parameters.second_fraction + second.first[i] * second_weight
                : second.first[i] * second_weight + second.second[i] * parameters.second_fraction;
        delta[i] = a - b;
    }
    return std::sqrt(dot_xyz(delta, delta));
}

RageSegmentClosest closest_rage_ray_segment(const RageRay& ray,
                                            const RageSegment& segment) noexcept {
    auto [s, t] = line_parameters(ray.start, ray.delta, segment);
    const auto ray_point = [&](float distance) {
        return RageVector{ray.delta[0] * distance + ray.start[0],
                          ray.delta[1] * distance + ray.start[1],
                          ray.delta[2] * distance + ray.start[2]};
    };
    if (s < 0.0f) {
        s = 0.0f;
        if (!outside(t)) {
            project_rage_point_to_segment(segment, ray.start, t);
            return {s, t, ray.start};
        }
        const auto& endpoint = t < 0.0f ? segment.first : segment.second;
        const float endpoint_fraction = t < 0.0f ? 0.0f : 1.0f;
        s = maximum_number(0.0f, dot_yxz(difference(endpoint, ray.start), ray.delta));
        const auto first_candidate = ray_point(s);
        const auto second_candidate = project_rage_point_to_segment(segment, ray.start, t);
        const auto first_delta = difference(first_candidate, endpoint);
        const auto second_delta = difference(second_candidate, ray.start);
        if (dot_yxz(second_delta, second_delta) >= dot_yxz(first_delta, first_delta))
            return {s, endpoint_fraction, first_candidate};
        return {0.0f, t, ray.start};
    }
    if (t < 0.0f) {
        t = 0.0f;
        s = maximum_number(0.0f, dot_yxz(difference(segment.first, ray.start), ray.delta));
    } else if (t > 1.0f) {
        t = 1.0f;
        s = maximum_number(0.0f, dot_yxz(difference(segment.second, ray.start), ray.delta));
    }
    return {s, t, ray_point(s)};
}
} // namespace skeet
