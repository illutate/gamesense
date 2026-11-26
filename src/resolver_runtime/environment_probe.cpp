#include "skeet/resolver_runtime/environment_probe.hpp"
#include "skeet/rage/point_penetration.hpp"
#include <cmath>
#include <xmmintrin.h>

namespace skeet::resolver_runtime {
namespace {
constexpr float epsilon = 1.1920928955078125e-7f;
RageVector subtract(const RageVector& a, const RageVector& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
RageVector normalize(const RageVector& value) {
    const float length =
        std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]);
    const float inverse = 1.0f / (length + epsilon);
    return {inverse * value[0], inverse * value[1], inverse * value[2]};
}
RageVector perpendicular(const RageVector& horizontal) {
    if (horizontal[0] == 0.0f && horizontal[1] == 0.0f)
        return {0.0f, -1.0f, 0.0f};
    const float vertical_product = horizontal[2] * 0.0f;
    return normalize({horizontal[1] - vertical_product, vertical_product - horizontal[0],
                      horizontal[0] * 0.0f - horizontal[1] * 0.0f});
}
EnvironmentRay ray(const RageVector& start, const RageVector& end, bool hull) {
    const auto delta = subtract(end, start);
    const bool swept = (delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2] != 0.0f;
    return {{start, delta},
            hull ? RageVector{-0.0f, -0.0f, -0.0f} : RageVector{},
            hull ? RageVector{16.0f, 16.0f, 2.0f} : RageVector{},
            !hull,
            swept};
}
struct Suppress {
    std::uint8_t* flag;
    std::uint8_t saved = 0;
    explicit Suppress(std::uint8_t* value) : flag(value) {
        if (flag != nullptr) {
            saved = *flag;
            *flag = 0;
        }
    }
    ~Suppress() {
        if (flag != nullptr)
            *flag = saved;
    }
};
}

EnvironmentProbe probe_environment(const RageVector& local_origin, const RageVector& target_origin,
                                   const EnvironmentEngine& engine) {
    Suppress suppress(engine.suppression_flag);
    EnvironmentProbe result{};
    const RageVector local{local_origin[0], local_origin[1], local_origin[2] + 64.0f};
    const RageVector target{target_origin[0], target_origin[1], target_origin[2] + 64.0f};
    const auto delta = subtract(target, local);
    BallisticTrace trace;
    engine.trace_ray(engine.context, {ray(local, target, false), 1, 0}, trace);
    if (trace.fraction == 1.0f && trace.start_solid == 0)
        return result;

    const auto direction = normalize(delta);
    RageVector angles;
    engine.view_angles(engine.context, angles);
    constexpr float radians = 0.01745329238474369f;
    const float sy = engine.trigonometry.sine(angles[1] * radians);
    const float cy = engine.trigonometry.cosine(angles[1] * radians);
    const float sp = engine.trigonometry.sine(angles[0] * radians);
    const float cp = engine.trigonometry.cosine(angles[0] * radians);
    const float facing = ((cp * sy) * direction[1] + (cp * cy) * direction[0]) - direction[2] * sp;
    if (!(facing >= 0.0f))
        return result;

    const auto horizontal = normalize({delta[0], delta[1], 0.0f});
    const auto side_direction = perpendicular(horizontal);
    constexpr float signs[2] = {1.0f, -1.0f};
    BallisticTrace hull_trace;
    for (std::size_t side = 0; side != 2; ++side) {
        RageVector endpoint;
        for (std::size_t axis = 0; axis != 3; ++axis)
            endpoint[axis] = side_direction[axis] * signs[side] * 48.0f + target[axis];
        const RageVector hull_start{target[0] + 0.0f, target[1] + 0.0f, target[2] + 0.0f};
        engine.trace_ray(engine.context, {ray(hull_start, endpoint, true), 1, 1}, hull_trace);
        result.shifted_target_points[side] =
            hull_trace.fraction < 1.0f || hull_trace.all_solid != 0 || hull_trace.start_solid != 0
                ? hull_trace.end
                : endpoint;
    }

    bool blocked[2] = {false, false};
    std::int32_t combined = 0;
    float offset = 0.0f;
    for (std::uint32_t step = 0; step != 3; ++step) {
        for (std::size_t side = 0; side != 2; ++side) {
            if (blocked[side])
                continue;
            const float distance = signs[side] * offset;
            RageVector sample;
            for (std::size_t axis = 0; axis != 3; ++axis)
                sample[axis] = side_direction[axis] * distance + local[axis];
            if ((engine.point_contents(engine.context, sample, 1) & 1U) != 0) {
                blocked[side] = true;
                if (blocked[0] && blocked[1]) {
                    if (engine.reworked())
                        return result;
                    // 43348FFC bypasses the perpendicular recomputation and
                    // reuses the current local-eye vector in native +74..+7C.
                    result.direction = local;
                    result.selected = true;
                    return result;
                }
            } else {
                for (std::size_t point = 0; point != 2; ++point) {
                    BallisticQuery query;
                    query.allow_penetration = 1;
                    query.weapon_id = 0;
                    query.penetration = 2.5f;
                    query.range = 8192.0f;
                    query.range_modifier = 0.98f;
                    query.armor_ratio = 1.65f;
                    query.base_damage = 80.0f;
                    query.reserved = 1;
                    trace_point_penetration(engine.penetration, sample,
                                            result.shifted_target_points[point], 1, query,
                                            engine.suppression_flag);
                    const auto damage = _mm_cvttss_si32(_mm_set_ss(query.damage));
                    combined |= damage;
                    result.damage[point] = damage;
                }
                if (combined != 0)
                    break;
            }
        }
        if (combined != 0)
            break;
        offset += 32.0f;
    }
    if (result.damage[0] == result.damage[1])
        return result;
    result.direction = perpendicular(horizontal);
    if (result.damage[1] == 0)
        for (auto& value : result.direction)
            value = -value;
    else if (result.damage[0] != 0)
        return result;
    result.selected = true;
    return result;
}
} // namespace skeet::resolver_runtime
