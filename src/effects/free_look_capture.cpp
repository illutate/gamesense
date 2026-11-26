#include "skeet/effects/free_look_capture.hpp"
#include "skeet/effects/free_look_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/collision.hpp"
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
void* base_trace_entity(void* candidate, const FreeLookCaptureBindings& b) {
    if (engine::invoke<bool>(b.engine_info, 3, candidate))
        return nullptr;
    return engine::invoke<void*>(candidate, 7);
}
struct CaptureTraceFilter {
    void* const* vtable;
    void* skip;
    std::uint32_t collision_group = 13, additional = 0;
    const FreeLookCaptureBindings* bindings;
};
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_CAPTURE_MEMBER __thiscall
#else
#define SKEET_CAPTURE_MEMBER
#endif
bool SKEET_CAPTURE_MEMBER accepts(void* receiver, void* candidate, std::uint32_t mask) {
    return free_look_capture_accepts(receiver, candidate, mask,
                                     *static_cast<CaptureTraceFilter*>(receiver)->bindings);
}
#undef SKEET_CAPTURE_MEMBER
struct alignas(16) CaptureEngineRay {
    std::array<float, 4> start{}, delta{}, start_offset{}, extents{};
    const void* world_axis_transform = nullptr;
    std::uint8_t is_ray = 0, swept = 1;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(CaptureTraceFilter, skip) == 4);
static_assert(offsetof(CaptureTraceFilter, collision_group) == 8);
static_assert(offsetof(CaptureTraceFilter, bindings) == 0x10);
static_assert(offsetof(CaptureEngineRay, world_axis_transform) == 0x40);
static_assert(offsetof(CaptureEngineRay, is_ray) == 0x44);
#endif
}

bool free_look_capture_accepts(void* receiver, void* candidate, std::uint32_t mask,
                               const FreeLookCaptureBindings& b) {
    if (!reinterpret_cast<engine::MemberFunction<bool, void*, std::uint32_t>>(
            b.trace_filter_vtable[0])(receiver, candidate, mask))
        return false;
    auto* entity = base_trace_entity(candidate, b);
    while (entity) {
        if (engine::invoke<bool>(entity, 162))
            return false;
        if (engine::invoke<bool>(entity, 158))
            return false;
        const auto handle = read<std::uint32_t>(entity, b.owner_handle);
        if (handle == UINT32_MAX)
            break;
        entity = engine::invoke<void*>(b.entity_list, 4, handle);
    }
    return true;
}

void capture_free_look_view_hook(void* player, void** original_table, void* setup,
                                 const FreeLookCaptureBindings& b) {
    reinterpret_cast<engine::MemberFunction<void, void*>>(original_table[337])(player, setup);
    if (engine::invoke<void*>(b.input, 32, std::int32_t(-1))) {
        std::array<float, 3> camera;
        engine::invoke<void>(b.input, 34, camera.data());
        constexpr float radians = 0.01745329238474369f;
        const float pitch = mul(camera[0], radians);
        const float sp = b.trigonometry.sine(pitch), cp = b.trigonometry.cosine(pitch);
        const float yaw = mul(camera[1], radians);
        const float sy = b.trigonometry.sine(yaw), cy = b.trigonometry.cosine(yaw);
        //43366128 calculates all three bases, including both roll imports.
        //The right and up vectors are local scratch and have no later reader.
        b.trigonometry.sine(0.0f);
        b.trigonometry.cosine(0.0f);
        const std::array<float, 3> forward{mul(cy, cp), mul(sy, cp), neg(sp)};
        std::array<float, 3> eye;
        engine::invoke<void>(player, 212, eye.data());
        eye[2] = sub(eye[2], sub(eye[2], read<float>(setup, 0xC0)));

        CaptureEngineRay ray;
        for (std::size_t axis = 0; axis != 3; ++axis) {
            ray.delta[axis] = sub(sub(eye[axis], mul(camera[2], forward[axis])), eye[axis]);
            ray.start[axis] = add(eye[axis], 0.0f);
            ray.start_offset[axis] = -0.0f;
            ray.extents[axis] = 9.0f;
        }
        const float squared =
            add(add(mul(ray.delta[1], ray.delta[1]), mul(ray.delta[0], ray.delta[0])),
                mul(ray.delta[2], ray.delta[2]));
        ray.swept = squared != 0.0f;
        //Slots1..3 are genuine engine entries. Only the recovered slot0 adds
        //the source owner-chain policy, with the exact native filter prefix.
        std::array<void*, 4> table{reinterpret_cast<void*>(&accepts), b.trace_filter_vtable[1],
                                   b.trace_filter_vtable[2], b.trace_filter_vtable[3]};
        CaptureTraceFilter filter{table.data(), player, 13, 0, &b};
        MovementCollisionTrace trace{};
        trace.hitgroup = -1;
        trace.hitbox = -1; //native fraction starts at zero
        engine::invoke<void>(b.engine_trace, 5, &ray, 0x200400Bu, &filter, &trace);
        if (1.0f > trace.fraction) {
            auto* input = b.input; //native reload after TraceRay
            write(input, 0xB4, mul(trace.fraction, read<float>(input, 0xB4)));
        }
    }
    auto* owner = b.free_look;
    const auto* frame = b.frame;
    auto& capture = free_look_owner_state(*owner).view;
    //Native first snapshots setup+B8, then writes three words sequentially.
    const auto captured = read<std::array<std::uint32_t, 3>>(setup, 0xB8);
    for (std::size_t axis = 0; axis != 3; ++axis)
        std::memcpy(&capture.origin[axis], &captured[axis], sizeof captured[axis]);
    capture.frame = *frame;
}

} //namespace skeet::effects
