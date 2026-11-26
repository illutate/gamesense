#include "skeet/misc/knife_melee.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
float add(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float subtract(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float multiply(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float inverse_length(float squared) noexcept {
    const auto denominator = _mm_add_ss(_mm_sqrt_ss(_mm_set_ss(squared)), _mm_set_ss(0x1p-23f));
    return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(1.0f), denominator));
}
bool greater(float a, float b) noexcept {
    return _mm_comigt_ss(_mm_set_ss(a), _mm_set_ss(b));
}
bool greater_equal(float a, float b) noexcept {
    return _mm_comige_ss(_mm_set_ss(a), _mm_set_ss(b));
}

// The four native 4346EDC0 slots are only jumps into this genuine engine
// table. Supplying the table directly retains that external boundary and the
// exact filter prefix (skip entity, collision group zero, additional word zero).
struct KnifeTraceFilter {
    void* const* vtable;
    void* skip;
    std::uint32_t collision_group = 0, additional = 0;
};
struct alignas(16) KnifeEngineRay {
    std::array<float, 4> start{}, delta{}, start_offset{}, extents{};
    const void* world_axis_transform = nullptr;
    std::uint8_t is_ray = 1, swept = 0;
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(offsetof(KnifeTraceFilter, skip) == 4);
static_assert(offsetof(KnifeTraceFilter, collision_group) == 8);
static_assert(offsetof(KnifeEngineRay, world_axis_transform) == 0x40);
static_assert(offsetof(KnifeEngineRay, is_ray) == 0x44);
#endif
KnifeEngineRay make_ray(const RageVector& start, const RageVector& end, bool hull) noexcept {
    KnifeEngineRay ray;
    for (unsigned axis = 0; axis != 3; ++axis) {
        ray.delta[axis] = subtract(end[axis], start[axis]);
        ray.start[axis] = hull ? add(start[axis], 0.0f) : start[axis];
        if (hull)
            ray.start_offset[axis] = -0.0f;
    }
    const auto squared =
        add(add(multiply(ray.delta[1], ray.delta[1]), multiply(ray.delta[0], ray.delta[0])),
            multiply(ray.delta[2], ray.delta[2]));
    ray.swept = squared != 0.0f;
    if (hull) {
        ray.extents = {16.0f, 16.0f, 18.0f, 0.0f};
        ray.is_ray = 0;
    }
    return ray;
}
RageVector endpoint(const RageVector& origin, const RageVector& direction, float reach) noexcept {
    return {add(multiply(direction[0], reach), origin[0]),
            add(multiply(direction[1], reach), origin[1]),
            add(multiply(direction[2], reach), origin[2])};
}
bool hit(const BallisticTrace& trace) noexcept {
    return greater(1.0f, trace.fraction) || trace.all_solid || trace.start_solid;
}
void* entity_pointer(BallisticEntity entity) noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(entity));
}
long double armor_ratio(void* weapon) {
#if defined(_M_IX86) || defined(__i386__)
    // The getter returns ST0. Native 4333CD68 multiplies that value directly,
    // retaining its extended precision until the following single float store.
    return engine::invoke<long double>(weapon, 434);
#else
    return engine::invoke<float>(weapon, 434);
#endif
}
}

void trace_knife_melee(void* local_player, const RageVector& start, const RageVector& end,
                       BallisticTrace& result, const KnifeMeleeBindings& bindings) {
    KnifeTraceFilter filter{bindings.trace_filter_vtable, local_player};
    auto ray = make_ray(start, end, false);
    engine::invoke<void>(bindings.engine_trace, 5, &ray, 0x200400Bu, &filter, &result);
    if (greater_equal(result.fraction, 1.0f)) {
        KnifeTraceFilter hull_filter{bindings.trace_filter_vtable, local_player};
        ray = make_ray(start, end, true);
        engine::invoke<void>(bindings.engine_trace, 5, &ray, 0x200400Bu, &hull_filter, &result);
    }
}

bool find_knife_trace_direction(void* local_player, const RageVector& origin, void* target,
                                RageVector& direction, const KnifeMeleeBindings& bindings) {
    BallisticTrace trace;
    auto end = endpoint(origin, direction, 48.0f);
    trace_knife_melee(local_player, origin, end, trace, bindings);
    if (!hit(trace))
        return false;
    if (entity_pointer(trace.entity) == target)
        return true;

    float yaw = 0.0f;
    if (direction[1] != 0.0f || direction[0] != 0.0f)
        yaw = float(bindings.arc_tangent2(double(direction[1]), double(direction[0])));
    constexpr float angle_step = 0.0872664600610733f;
    float offset = angle_step;
    for (unsigned step = 0; step != 7; ++step) {
        for (unsigned side = 0; side != 2; ++side) {
            const auto angle = side == 0 ? add(offset, yaw) : subtract(yaw, offset);
            const auto sine = bindings.trigonometry.sine(angle);
            const auto cosine = bindings.trigonometry.cosine(angle);
            const auto vertical = direction[2];
            const auto inverse = inverse_length(add(
                add(multiply(sine, sine), multiply(cosine, cosine)), multiply(vertical, vertical)));
            const RageVector candidate{multiply(inverse, cosine), multiply(inverse, sine),
                                       multiply(vertical, inverse)};
            end = endpoint(origin, candidate, 48.0f);
            trace_knife_melee(local_player, origin, end, trace, bindings);
            if (hit(trace) && entity_pointer(trace.entity) == target) {
                direction = candidate;
                return true;
            }
        }
        offset = add(offset, angle_step);
    }
    return false;
}

std::uint32_t score_knife_attack(const client::CommandFrame& frame, void* local_player,
                                 const RageVector& direction, bool stab,
                                 const KnifeMeleeBindings& bindings) {
    BallisticTrace trace;
    const auto end = endpoint(frame.origin_dc, direction, stab ? 32.0f : 48.0f);
    trace_knife_melee(local_player, frame.origin_dc, end, trace, bindings);
    if (greater_equal(trace.fraction, 1.0f))
        return 0;
    auto* target = entity_pointer(trace.entity);
    if (!rage::target_is_eligible(bindings.targeting, target))
        return 0;

    const auto* angles = engine::invoke<const RageVector*>(target, 11);
    constexpr float radians = 0.01745329238474369f;
    const auto yaw = multiply((*angles)[1], radians);
    const auto sine_yaw = bindings.trigonometry.sine(yaw);
    const auto cosine_yaw = bindings.trigonometry.cosine(yaw);
    const auto pitch = multiply((*angles)[0], radians);
    bindings.trigonometry.sine(pitch);
    const auto cosine_pitch = bindings.trigonometry.cosine(pitch);
    const auto forward_x = multiply(cosine_pitch, cosine_yaw);
    const auto forward_y = multiply(cosine_pitch, sine_yaw);

    const auto x = subtract(field<float>(target, bindings.target_origin), frame.origin_10c[0]);
    const auto y = subtract(field<float>(target, bindings.target_origin + 4), frame.origin_10c[1]);
    const auto inverse = inverse_length(add(multiply(y, y), multiply(x, x)));
    const auto unit_x = multiply(x, inverse), unit_y = multiply(y, inverse);
    std::uint32_t score = 2;
    if (stab)
        score = engine::invoke<std::int32_t>(target, 122) > 100 ? 9 : 1;

    auto* weapon = entity_pointer(frame.weapon_address);
    std::int32_t damage;
    const auto facing = add(multiply(unit_y, forward_y), multiply(forward_x, unit_x));
    if (greater(facing, 0.4749999940395355f)) {
        score |= 4;
        damage = stab ? 180 : 90;
    } else if (stab)
        damage = 65;
    else {
        const auto next_attack =
            add(field<float>(weapon, bindings.weapon_next_primary), 0.4000000059604645f);
        const auto now = *bindings.current_time;
        damage = greater(now, next_attack) ? 40 : 25;
    }
    if (field<std::int32_t>(target, bindings.target_armor) > 0) {
        const auto ratio = armor_ratio(weapon);
        const float scaled = float((ratio * static_cast<long double>(damage)) * 0.5L);
        damage = _mm_cvttss_si32(_mm_set_ss(scaled));
    }
    if (damage >= engine::invoke<std::int32_t>(target, 122))
        score |= 0x10;
    return score;
}
} // namespace skeet::misc
