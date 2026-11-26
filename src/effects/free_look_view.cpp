#include "skeet/effects/free_look_view.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/collision.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::effects {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
float add(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float sub(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float mul(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float neg(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float clamp(float value, float low, float high) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(low)), _mm_set_ss(high)));
}
void normalize(float* angles) noexcept {
    angles[0] = normalize_movement_angle(angles[0]);
    angles[1] = normalize_movement_angle(angles[1]);
    angles[2] = normalize_movement_angle(angles[2]);
}
const void* parent(const void* variable) noexcept {
    for (;;) {
        auto* next = read<void*>(variable, 0x1C);
        if (next == variable)
            return variable;
        variable = next;
    }
}
float encoded_float(const void* variable) noexcept {
    auto* root = parent(variable);
    return std::bit_cast<float>(read<std::uint32_t>(root, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(root)));
}
bool encoded_integer_is_zero(const void* variable) noexcept {
    auto* root = parent(variable);
    return read<std::uint32_t>(root, 0x30) == std::uint32_t(reinterpret_cast<std::uintptr_t>(root));
}
//4346EDC0's four entries only forward to this external engine filter table.
struct FreeLookTraceFilter {
    void* const* vtable;
    void* skip;
    std::uint32_t collision_group = 0, additional = 0;
};
struct alignas(16) FreeLookEngineRay {
    std::array<float, 4> start{}, delta{}, start_offset{}, extents{};
    const void* world_axis_transform = nullptr;
    std::uint8_t is_ray = 0, swept = 1;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(FreeLookTraceFilter, skip) == 4);
static_assert(offsetof(FreeLookEngineRay, world_axis_transform) == 0x40);
static_assert(offsetof(FreeLookEngineRay, is_ray) == 0x44);
#endif
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_FREE_PHASE __thiscall
#define SKEET_FREE_FAST __fastcall
#else
#define SKEET_FREE_PHASE
#define SKEET_FREE_FAST
#endif
void SKEET_FREE_PHASE empty_phase(void*) {} //433CCE8E
void SKEET_FREE_FAST empty_player(void*, void*, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
} //4337D653
void SKEET_FREE_FAST empty_post(void*, void*) {} //433CCE8E
void SKEET_FREE_FAST view(void* receiver, float* origin, float* angles, float*) {
    static_cast<FreeLookViewObserver*>(receiver)->runtime->update(origin, angles);
}
const network::UpdateObserverVtable free_look_vtable{
    {empty_phase, empty_phase, empty_phase, empty_phase, empty_phase},
    empty_player,
    view,
    empty_post};
#undef SKEET_FREE_PHASE
#undef SKEET_FREE_FAST
}

void FreeLookViewRuntime::reset() noexcept {
    state.delta = {0.0f, 0.0f, 0.0f};
    state.active = 0;
}
bool FreeLookViewRuntime::third_person_active() {
    if (engine::invoke<void*>(bindings.input, 32, std::int32_t(-1)))
        return true;
    return bindings.third_person && hotkey_active(bindings.third_person_key, bindings.keys);
}
void FreeLookViewRuntime::update(float* origin, float* angles) {
    if (!hotkey_active(bindings.free_look_key, bindings.keys) || !state.active) {
        reset();
        return;
    }
    auto* player = bindings.local_player;
    if (!player || read<std::uint8_t>(player, bindings.life_state)) {
        reset();
        return;
    }
    angles[0] = add(angles[0], state.delta[0]);
    angles[1] = add(state.delta[1], angles[1]);
    normalize(angles);
    angles[0] = clamp(angles[0], -89.0f, 89.0f);
    if (third_person_active() && capture.frame == bindings.frame) {
        //The input output is intentionally discarded; this virtual call still
        //precedes the player query and can change live engine bindings.
        std::array<float, 3> unused;
        engine::invoke<void>(bindings.input, 34, unused.data());
        const float saved_height = capture.origin[2];
        std::array<float, 3> eye;
        engine::invoke<void>(player, 212, eye.data());
        origin[0] = eye[0];
        origin[1] = eye[1];
        origin[2] = eye[2];
        origin[2] = sub(origin[2], sub(origin[2], saved_height));

        constexpr float radians = 0.01745329238474369f;
        const float yaw = mul(angles[1], radians);
        const float sine_yaw = bindings.trigonometry.sine(yaw);
        const float cosine_yaw = bindings.trigonometry.cosine(yaw);
        const float pitch = mul(angles[0], radians);
        const float sine_pitch = bindings.trigonometry.sine(pitch);
        const float cosine_pitch = bindings.trigonometry.cosine(pitch);
        const std::array<float, 3> forward{mul(cosine_pitch, cosine_yaw),
                                           mul(cosine_pitch, sine_yaw), neg(sine_pitch)};
        const float distance = encoded_float(bindings.camera_distance);
        FreeLookEngineRay ray;
        for (std::size_t axis = 0; axis != 3; ++axis) {
            ray.start[axis] = add(origin[axis], 0.0f);
            ray.delta[axis] = sub(sub(origin[axis], mul(forward[axis], distance)), origin[axis]);
            ray.start_offset[axis] = -0.0f;
            ray.extents[axis] = 9.0f;
        }
        const float squared =
            add(add(mul(ray.delta[1], ray.delta[1]), mul(ray.delta[0], ray.delta[0])),
                mul(ray.delta[2], ray.delta[2]));
        ray.swept = squared != 0.0f;
        FreeLookTraceFilter filter{bindings.trace_filter_vtable, player};
        MovementCollisionTrace trace{};
        trace.fraction = 1.0f;
        trace.hitgroup = -1;
        trace.hitbox = -1;
        engine::invoke<void>(bindings.engine_trace, 5, &ray, 0x200400Bu, &filter, &trace);
        const float actual_distance =
            1.0f > trace.fraction ? mul(trace.fraction, distance) : distance;
        const float backward = neg(actual_distance);
        origin[0] = add(mul(backward, forward[0]), origin[0]);
        origin[1] = add(mul(backward, forward[1]), origin[1]);
        origin[2] = add(mul(backward, forward[2]), origin[2]);
    }
    if (third_person_active())
        return;
    const bool mode_zero = encoded_integer_is_zero(bindings.camera_mode);
    const float yaw_scale = mode_zero ? 1.0f : -1.0f;
    const float yaw_minimum = mode_zero ? -20.0f : -60.0f;
    const float yaw_maximum = mode_zero ? 60.0f : 20.0f;
    auto* prediction = bindings.prediction;
    const auto previous = read<std::uint8_t>(prediction, 8);
    write(prediction, 8, std::uint8_t(1));
    std::array<float, 3> local_angles{angles[0], angles[1], angles[2]};
    local_angles[0] = add(clamp(neg(state.delta[0]), -30.0f, 10.0f), local_angles[0]);
    local_angles[1] =
        add(clamp(mul(state.delta[1], yaw_scale), yaw_minimum, yaw_maximum), local_angles[1]);
    normalize(local_angles.data());
    local_angles[0] = clamp(local_angles[0], -89.0f, 89.0f);
    engine::invoke<void>(player, 278, origin, local_angles.data());
    write(prediction, 8, previous);
}
FreeLookViewObserver::FreeLookViewObserver(FreeLookViewRuntime& runtime, bool initialize_table)
    : endpoint{initialize_table ? &free_look_vtable : nullptr}, runtime(&runtime) {}
void FreeLookViewObserver::initialize_vtable() noexcept {
    endpoint.vtable = &free_look_vtable;
}

} //namespace skeet::effects
