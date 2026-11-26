#include "skeet/rage_safe_points.hpp"
#include "skeet/rage_segments.hpp"
#include "rage_safe_point_detail.hpp"
#include <cmath>

namespace skeet {
namespace rage_point_detail {
RageCapsule transform_capsule(const RageLocalHitbox& local, const Matrix3x4& matrix) noexcept {
    RageCapsule capsule{{}, {}, local.radius};
    for (unsigned r = 0; r < 3; ++r) {
        const auto& m = matrix.rows[r];
        capsule.first[r] =
            ((m[1] * local.minimum[1] + m[0] * local.minimum[0]) + m[2] * local.minimum[2]) + m[3];
        capsule.second[r] =
            ((m[1] * local.maximum[1] + m[0] * local.maximum[0]) + m[2] * local.maximum[2]) + m[3];
    }
    return capsule;
}
namespace {
RageVector clamp_to_capsule(const RageVector& point, const RageCapsule& capsule) noexcept {
    float fraction;
    const auto closest =
        project_rage_point_to_segment({capsule.first, capsule.second}, point, fraction);
    const auto relative = subtract(closest, point);
    const float squared =
        (relative[1] * relative[1] + relative[0] * relative[0]) + relative[2] * relative[2];
    if (capsule.radius * capsule.radius >= squared)
        return point;
    const auto direction = subtract(point, closest);
    const float magnitude_squared =
        (direction[1] * direction[1] + direction[0] * direction[0]) + direction[2] * direction[2];
    // The tiny-distance branch keeps radius in the X component, as in the
    // original 433FE77C/433FE8CD branches, rather than inventing an axis.
    RageVector offset{capsule.radius, 0.0f, 0.0f};
    if (magnitude_squared >= 1.0e-6f) {
        const float factor = capsule.radius / std::sqrt(magnitude_squared);
        offset = {factor * direction[0], factor * direction[1], factor * direction[2]};
    }
    return {closest[0] + offset[0], offset[1] + closest[1], closest[2] + offset[2]};
}
}

SafePointBounds build_safe_point_bounds(const RageVector& origin, const RageVector& center,
                                        const RageLocalHitbox& local,
                                        std::span<const Matrix3x4> hypotheses) {
    if (hypotheses.empty() || !(local.radius > 0.0f))
        return {};
    const auto relative = subtract(center, origin);
    const auto basis = basis_from_axis(normalize(relative, length(relative)));
    const RageVector opposite{-basis.side[0], -basis.side[1], -basis.side[2]};
    float upper = 1.0f, lower = -1.0f;
    RageVector upper_point{}, lower_point{};
    std::array<RageCapsule, 3> last_hypotheses;
    for (std::size_t i = 0; i < hypotheses.size(); ++i) {
        const auto capsule = transform_capsule(local, hypotheses[i]);
        last_hypotheses[i % 3] = capsule;
        const auto positive = support(capsule, basis.side, true);
        const auto negative = support(capsule, opposite, true);
        const auto first = subtract(positive, origin);
        const auto second = subtract(negative, origin);
        const float high =
            ((basis.side[1] * first[1] + basis.side[0] * first[0]) + basis.side[2] * first[2]) /
            length(first);
        const float low =
            ((basis.side[1] * second[1] + basis.side[0] * second[0]) + basis.side[2] * second[2]) /
            length(second);
        if (upper > high) {
            upper = high;
            upper_point = positive;
        }
        if (low > lower) {
            lower = low;
            lower_point = negative;
        }
    }
    if (upper == 1.0f || lower == -1.0f || lower >= upper)
        return {};
    return {last_hypotheses,
            basis,
            {{lower_point, 1.0f, local.hitgroup, 3}, {upper_point, 1.0f, local.hitgroup, 3}}};
}

std::vector<RageAimPoint> finish_safe_points(std::vector<RageAimPoint> result,
                                             const RagePointGeneration& settings,
                                             const RageVector& target_origin,
                                             std::uint32_t hitbox_index, std::int32_t hitgroup) {
    if (result.empty())
        return result;
    RageVector average{};
    for (const auto& point : result)
        for (unsigned i = 0; i < 3; ++i)
            average[i] += point.position[i] - target_origin[i];
    const float inverse = 1.0f / float(result.size());
    for (unsigned i = 0; i < 3; ++i)
        average[i] = inverse * average[i] + target_origin[i];
    const auto group_bit =
        hitbox_index == 11 || hitbox_index == 12 ? 0x200u : 1u << (std::uint32_t(hitgroup) & 31u);
    const bool multipoint =
        ((settings.multipoint_mask & group_bit) & 0xeu) != 0 ||
        ((settings.multipoint_mask & 0xc0u) != 0 && (hitbox_index == 7 || hitbox_index == 8));
    if (multipoint)
        scale_points(result, average, settings);
    else
        result.clear();
    result.insert(result.begin(), {average, 0.0f, hitgroup, 1});
    return result;
}
} // namespace rage_point_detail

std::vector<RageAimPoint> generate_rage_body_safe_points(const RagePointGeneration& settings,
                                                         const RageVector& center,
                                                         const RageVector& target_origin,
                                                         std::uint32_t hitbox_index,
                                                         const RageLocalHitbox& local,
                                                         std::span<const Matrix3x4> hypotheses) {
    using namespace rage_point_detail;
    auto bounds = build_safe_point_bounds(settings.origin, center, local, hypotheses);
    if (bounds.points.empty())
        return {};
    if (hitbox_index == 7 || hitbox_index == 8) {
        for (const auto& capsule : bounds.last_hypotheses)
            for (auto& point : bounds.points)
                point.position = clamp_to_capsule(point.position, capsule);
    }
    return finish_safe_points(std::move(bounds.points), settings, target_origin, hitbox_index,
                              local.hitgroup);
}
} // namespace skeet
