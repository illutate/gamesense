#include "skeet/rage/point_penetration.hpp"
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace skeet {
namespace {
struct RestoreSuppression {
    std::uint8_t* flag;
    std::uint8_t saved = 0;
    explicit RestoreSuppression(std::uint8_t* value) : flag(value) {
        if (flag) {
            saved = *flag;
            *flag = 0;
        }
    }
    ~RestoreSuppression() {
        if (flag)
            *flag = saved;
    }
};
BallisticVector subtract(const BallisticVector& a, const BallisticVector& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float length(const BallisticVector& value) noexcept {
    return std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]);
}
BallisticVector advance(const BallisticVector& position, const BallisticVector& direction,
                        float distance) noexcept {
    return {direction[0] * distance + position[0], direction[1] * distance + position[1],
            direction[2] * distance + position[2]};
}
}
std::int32_t trace_point_penetration(BallisticEngine& engine, const BallisticVector& start,
                                     const BallisticVector& target, std::int32_t requested_damage,
                                     BallisticQuery& query, std::uint8_t* suppression_flag) {
    RestoreSuppression suppression(suppression_flag);
    reset_ballistic_query(query);
    const BallisticFilter filter{nullptr,   0,    0x600400b,
                                 0x600400b, 1.0f, BallisticFilterKind::world_only};
    const auto offset = subtract(target, start);
    const float distance = length(offset);
    const float inverse = 1.0f / (distance + std::numeric_limits<float>::epsilon());
    const BallisticVector direction{offset[0] * inverse, offset[1] * inverse, offset[2] * inverse};
    const float minimum = maximum_number(1.0f, float(requested_damage) - 5.0f);
    float damage = 0.0f;
    if (!(distance > query.range) && !(minimum > query.base_damage)) {
        damage = query.base_damage;
        BallisticVector position = start;
        float travelled = 0.0f;
        for (;;) {
            const float remaining = query.range - travelled;
            const auto endpoint = advance(position, direction, remaining);
            const auto delta = subtract(endpoint, position);
            const bool swept =
                (delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2] != 0.0f;
            engine.trace_ray({{position, delta, swept}, filter.mask, filter.kind, &filter, 0, 0},
                             query.trace);
            if (length(subtract(query.trace.end, start)) >= distance) {
                const float fraction =
                    length(subtract(position, target)) / length(subtract(position, endpoint));
                const float exponent = (fraction * remaining + travelled) * 0.002f;
                damage = float(std::pow(double(query.range_modifier), double(exponent)) *
                               double(damage));
                query.trace.end = target;
                break;
            }
            travelled = query.trace.fraction * remaining + travelled;
            damage = float(std::pow(double(query.range_modifier), double(travelled * 0.002f)) *
                           double(damage));
            if (query.trace.fraction == 1.0f)
                break;
            query.points[query.point_count++] = query.trace.end;
            if (minimum > damage) {
                damage = 0.0f;
                break;
            }
            const auto surface = engine.surface(query.trace.surface_property);
            if ((travelled > 3000.0f && query.penetration > 0.0f) ||
                double(surface.penetration_modifier) < 0.1) {
                query.remaining_penetrations = 0;
                damage = 0.0f;
                break;
            }
            if (query.remaining_penetrations <= 0) {
                damage = 0.0f;
                break;
            }
            if (handle_ballistic_penetration(engine, direction, position, 0, filter, query, surface,
                                             damage)) {
                query.flags |= 4;
                damage = 0.0f;
                break;
            }
            if (minimum > damage) {
                damage = 0.0f;
                break;
            }
        }
        if (damage < 0.0f)
            damage = 0.0f;
    }
    query.damage = damage;
    return _mm_cvttss_si32(_mm_set_ss(damage));
}
} // namespace skeet
