#include "skeet/rage_geometry.hpp"
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace skeet {
namespace {
constexpr float epsilon = 0x1p-23f;
constexpr float maximum_float = std::numeric_limits<float>::max();
float dot_yxz(const RageVector& a, const RageVector& b) noexcept {
    return (a[1] * b[1] + a[0] * b[0]) + a[2] * b[2];
}
float length_yzx(const RageVector& value) noexcept {
    return std::sqrt((value[1] * value[1] + value[2] * value[2]) + value[0] * value[0]);
}
RageVector subtract(const RageVector& a, const RageVector& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
RageVector scale(const RageVector& value, float factor) noexcept {
    return {value[0] * factor, value[1] * factor, value[2] * factor};
}
RageVector cross(const RageVector& a, const RageVector& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float clamp_sse(float value, float limit) noexcept {
    const auto low = _mm_max_ss(_mm_set_ss(value), _mm_setzero_ps());
    return _mm_cvtss_f32(_mm_min_ss(low, _mm_set_ss(limit)));
}
// 0x433C918F. The source builds and orthogonalizes this seed vector, including
// its zero component; replacing it with a conventional cross product differs.
RageVector perpendicular(const RageVector& axis) noexcept {
    const float seed_x = (1.0f - axis[2]) * (axis[1] * axis[1] - 0.0f) + axis[2];
    const float seed_z = -axis[0];
    const float inverse = 1.0f / (std::sqrt((seed_x * seed_x + 0.0f) + seed_z * seed_z) + epsilon);
    const RageVector seed{seed_x * inverse, inverse * 0.0f, seed_z * inverse};
    const float projection = (seed[1] * axis[1] + axis[0] * seed[0]) + seed[2] * axis[2];
    const RageVector rejected{seed[0] - axis[0] * projection, seed[1] - axis[1] * projection,
                              seed[2] - axis[2] * projection};
    const float normalized =
        1.0f / (std::sqrt((rejected[0] * rejected[0] + rejected[1] * rejected[1]) +
                          rejected[2] * rejected[2]) +
                epsilon);
    return scale(rejected, normalized);
}
}

CapsuleIntersection intersect_rage_capsule(const RageRay& ray,
                                           const RageCapsule& capsule) noexcept {
    CapsuleIntersection result;
    const auto segment = subtract(capsule.second, capsule.first);
    const float segment_length = length_yzx(segment);
    auto relative = subtract(ray.start, capsule.first);
    const float radius = capsule.radius;
    if (radius < 0.00001f)
        return result;
    if (0.001f > segment_length) {
        const float speed_squared = dot_yxz(ray.delta, ray.delta);
        const float offset_squared = dot_yxz(relative, relative);
        const float outside = offset_squared - radius * radius;
        if (!(0x1p-46f > speed_squared)) {
            const float along = dot_yxz(ray.delta, relative);
            const float discriminant = along * along - outside * speed_squared;
            if (!(0.0f > discriminant)) {
                const float fraction = (-along - std::sqrt(discriminant)) / speed_squared;
                if (!(0.0f > fraction)) {
                    result.fraction = fraction;
                    for (unsigned i = 0; i < 3; ++i) {
                        const float displacement = ray.delta[i] * fraction;
                        result.point[i] = ray.start[i] + displacement;
                        result.normal[i] = (displacement + relative[i]) * (1.0f / radius);
                    }
                    return result;
                }
                if (!(outside <= 0.0f)) {
                    result.fraction = maximum_float;
                    return result;
                }
            } else {
                result.fraction = maximum_float;
                return result;
            }
        } else if (!(outside <= 0.0f)) {
            result.fraction = maximum_float;
            return result;
        }
        result.fraction = 0.0f;
        result.point = ray.start;
        result.normal = offset_squared > epsilon ? scale(relative, 1.0f / std::sqrt(offset_squared))
                                                 : RageVector{0, 0, 1};
        return result;
    }

    const auto axis = scale(segment, 1.0f / segment_length);
    const float ray_length = length_yzx(ray.delta);
    if (!(ray_length > 0.0001f)) {
        const float along = dot_yxz(relative, axis);
        if (!(0.0f > along)) {
            if (!(along <= segment_length))
                relative = subtract(ray.start, capsule.second);
            else {
                // Native 0x43419186 uses the first endpoint, not the unit axis.
                for (unsigned i = 0; i < 3; ++i)
                    relative[i] -= capsule.first[i] * along;
            }
        }
        const float offset_squared = dot_yxz(relative, relative);
        // The native stationary path compares squared distance with radius.
        if (!(offset_squared <= radius))
            return result;
        result.fraction = 0.0f;
        result.start_solid = true;
        result.normal = offset_squared > 1.0e-8f ? scale(relative, 1.0f / std::sqrt(offset_squared))
                                                 : perpendicular(axis);
        result.point = ray.start;
        return result;
    }

    const auto direction = scale(ray.delta, 1.0f / ray_length);
    auto plane_normal = cross(axis, direction);
    const float normal_squared = dot_yxz(plane_normal, plane_normal);
    const float axis_motion = dot_yxz(ray.delta, axis);
    float fraction = maximum_float;
    float axis_position = 0;
    if (!(normal_squared > 0x1p-30f)) {
        const float projection = dot_yxz(relative, axis);
        RageVector radial;
        for (unsigned i = 0; i < 3; ++i)
            radial[i] = relative[i] - axis[i] * projection;
        const float radial_squared = dot_yxz(radial, radial);
        float extent = radius;
        bool has_extent = true;
        if (!(0x1p-46f > radial_squared)) {
            const auto radial_direction = scale(radial, 1.0f / std::sqrt(radial_squared));
            const auto normal = cross(radial_direction, axis);
            const float off_plane = dot_yxz(relative, normal);
            const float available = (radius * radius - radial_squared) - off_plane * off_plane;
            if (!(available > 0.0f)) {
                axis_position = radial_squared;
                has_extent = false;
            } else
                extent = std::sqrt(available);
        }
        if (has_extent) {
            if (0.0f > projection + extent) {
                const float distance = -(projection + extent);
                axis_position = 0.0f;
                if (!(axis_motion < distance))
                    fraction = distance / axis_motion;
            } else if (segment_length + extent > projection) {
                axis_position = clamp_sse(projection, segment_length);
                fraction = 0.0f;
            } else {
                const float distance = projection - (segment_length + extent);
                const float backward_motion = -axis_motion;
                axis_position = segment_length;
                if (!(backward_motion < distance))
                    fraction = distance / backward_motion;
            }
        }
    } else {
        plane_normal = scale(plane_normal, 1.0f / std::sqrt(normal_squared));
        const float off_plane = dot_yxz(relative, plane_normal);
        const float available = radius * radius - off_plane * off_plane;
        if (0.0f >= available) {
            result.fraction = maximum_float;
            return result;
        }
        const auto lateral_axis = cross(plane_normal, axis);
        float projection = dot_yxz(relative, axis);
        const float lateral = dot_yxz(relative, lateral_axis);
        const float lateral_motion = dot_yxz(ray.delta, lateral_axis);
        const float closest_axis_position = clamp_sse(projection, segment_length);
        const float beyond_axis = projection - closest_axis_position;
        if (available > beyond_axis * beyond_axis + lateral * lateral) {
            fraction = 0.0f;
            axis_position = closest_axis_position;
        } else {
            const float cylinder_fraction = (-std::sqrt(available) - lateral) / lateral_motion;
            const float inverse_motion =
                1.0f / (lateral_motion * lateral_motion + axis_motion * axis_motion);
            const float cylinder_axis_position =
                maximum_number(0.0f, cylinder_fraction) * axis_motion + projection;
            bool endpoint = true;
            if (!(0.0f > cylinder_axis_position)) {
                if (!(segment_length <= cylinder_axis_position)) {
                    axis_position = cylinder_axis_position;
                    if (!(cylinder_fraction < 0.0f))
                        fraction = cylinder_fraction;
                    endpoint = false;
                } else {
                    axis_position = segment_length;
                    projection -= segment_length;
                }
            }
            if (endpoint) {
                const float along =
                    (projection * axis_motion + lateral_motion * lateral) * inverse_motion;
                const float discriminant =
                    along * along -
                    ((projection * projection + lateral * lateral) - available) * inverse_motion;
                if (!(discriminant < 0.0f)) {
                    const float endpoint_fraction = -along - std::sqrt(discriminant);
                    if (!(endpoint_fraction < 0.0f))
                        fraction = endpoint_fraction;
                }
            }
        }
    }
    result.fraction = fraction;
    RageVector normal;
    for (unsigned i = 0; i < 3; ++i) {
        result.point[i] = fraction * ray.delta[i] + ray.start[i];
        normal[i] = result.point[i] - (axis_position * axis[i] + capsule.first[i]);
    }
    const float inverse_normal =
        1.0f / (std::sqrt((normal[0] * normal[0] + normal[1] * normal[1]) + normal[2] * normal[2]) +
                epsilon);
    result.normal = scale(normal, inverse_normal);
    return result;
}

int clip_rage_ray_to_capsule(const RageRay& ray, const RageCapsule& capsule,
                             RageTrace& trace) noexcept {
    RageRay shortened = ray;
    shortened.delta = scale(ray.delta, trace.fraction);
    const auto result = intersect_rage_capsule(shortened, capsule);
    if (!(1.0f > result.fraction))
        return -1;
    trace.fraction *= result.fraction;
    if (result.start_solid)
        trace.start_solid = true;
    trace.point = result.point;
    trace.normal = result.normal;
    return 0;
}
} // namespace skeet
