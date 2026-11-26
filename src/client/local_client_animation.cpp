#include "skeet/client/local_player_animation_hooks.hpp"
#include "skeet/engine/trace.hpp"
#include "skeet/movement/collision.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
float convar_float(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
void original(void* player) {
    const auto* metadata =
        static_cast<const engine::VtableHookMetadata*>((*static_cast<void***>(player))[-2]);
    reinterpret_cast<engine::MemberFunction<void>>(metadata->original_table[224])(player);
}
void normalize_camera(void* input, std::ptrdiff_t offset) noexcept {
    auto angle = field<float>(input, offset);
    if (angle >= 180.0f)
        angle -= 360.0f;
    else if (-180.0f >= angle)
        angle += 360.0f;
    else
        return;
    write(input, offset, angle);
}
void update_camera(void* player, const LocalPlayerAnimationHookBindings& b) {
    const auto saved = b.view.begin_view_height(player, b.duck);
    {
        MovementTraceFlagGuard guard(b.trace_flag);
        auto* input = b.input;
        write(input, 0xB4, convar_float(b.camera_distance));
        MovementVector forward;
        movement_angle_vectors({field<float>(input, 0xAC), field<float>(input, 0xB0), 0.0f},
                               &forward, nullptr, nullptr);
        const auto absolute = *engine::invoke<const std::array<float, 3>*>(player, 10);
        const auto offset = field<std::array<float, 3>>(player, b.offsets.view_offset_c246315e);
        const auto distance = field<float>(input, 0xB4);
        const std::array<float, 3> eye{absolute[0] + offset[0], absolute[1] + offset[1],
                                       absolute[2] + offset[2]};
        engine::TraceRay ray;
        ray.start = {eye[0] + 0.0f, eye[1] + 0.0f, eye[2] + 0.0f, 0.0f};
        ray.delta = {(eye[0] - forward.x * distance) - eye[0],
                     (eye[1] - forward.y * distance) - eye[1],
                     (eye[2] - forward.z * distance) - eye[2], 0.0f};
        ray.start_offset = {-0.0f, -0.0f, -0.0f, 0.0f};
        ray.extents = {9.0f, 9.0f, 9.0f, 0.0f};
        ray.is_ray = 0;
        ray.swept = ((ray.delta[0] * ray.delta[0] + ray.delta[1] * ray.delta[1]) +
                     ray.delta[2] * ray.delta[2]) != 0.0f;
        std::array<std::byte, engine::trace_result_size> trace;
        write(trace.data(), 0x2C, 1.0f);
        write(trace.data(), 0x36, std::uint16_t(0));
        write(trace.data(), 0x4C, std::uint32_t(0));
        write(trace.data(), 0x44, UINT32_MAX);
        write(trace.data(), 0x50, UINT32_MAX);
        engine::TraceRejectEntitiesFilter filter;
        engine::invoke<void>(b.engine_trace, 5, &ray, std::uint32_t(0x200400B), &filter,
                             trace.data());
        const auto fraction = field<float>(trace.data(), 0x2C);
        if (1.0f > fraction)
            write(input, 0xB4, field<float>(input, 0xB4) * fraction);
        normalize_camera(input, 0xAC);
        normalize_camera(input, 0xB0);
    }
    b.view.restore_view_height(saved);
}
}
void LocalPlayerAnimationHooks::update_client_animation(void* player) {
    const auto& b = bindings;
    const auto& o = b.offsets;
    const bool camera_enabled = b.third_person != 0;
    const auto dead = field<std::uint8_t>(player, o.life_state_9d920e35);
    if (camera_enabled) {
        bool camera = !dead;
        if (dead) {
            const auto mode = field<std::int32_t>(player, o.observer_mode_2f4e97c4);
            camera = mode == 1 || mode == 2;
        }
        if (camera && engine::invoke<std::uint32_t>(b.input, 32, std::int32_t(-1)))
            update_camera(player, b);
    }
    if (!dead && (b.command_mode || b.alternate_command_mode)) {
        if (b.set_absolute_angles)
            b.set_absolute_angles(player, b.view_angles);
        const auto model = field<std::uint32_t>(player, o.model_index_a25d8271);
        write(player, o.model_index_a25d8271, UINT32_MAX);
        auto* state = field<void*>(player, o.animation_state_3d52a1a7);
        std::uint32_t saved_state;
        if (state) {
            saved_state = field<std::uint32_t>(state, 0x60);
            write(state, 0x60, std::uint32_t(0));
        }
        original(player);
        if (state)
            write(state, 0x60, saved_state);
        write(player, o.model_index_a25d8271, model);
        return;
    }
    const auto* state = field<const void*>(player, o.animation_state_3d52a1a7);
    const bool publish = !dead && state && field<std::uint32_t>(state, 0x60) &&
                         b.animation_frame != field<std::int32_t>(state, 0x70) &&
                         b.current_time != field<float>(state, 0x6C);
    original(player);
    if (publish) {
        auto angles = field<std::array<float, 3>>(player, o.angles_3b1f6ecc);
        for (auto& angle : angles)
            angle = normalize_movement_angle(angle);
        const bool invalidate = b.third_person != 0;
        b.view_angles = angles;
        if (invalidate && hotkey_active(b.third_person_key, b.keys) &&
            field<std::int32_t>(player, o.movement_mode_8b5586f6) == 3 &&
            field<std::uint8_t>(player, o.ducking_a15c3105)) {
            write(player, o.bone_accessor_54783141 + 8, std::uint32_t(0));
            write(player, o.bone_field_2b554ad0, std::uint32_t(0));
            write(player, o.bone_time_c7e1ecd2, std::uint32_t(0xFF7FFFFF));
            write(player, o.bone_counter_0471e13c, std::uint32_t(0));
            write(player, o.setup_flags_bb78dc43,
                  field<std::uint32_t>(player, o.setup_flags_bb78dc43) & ~2U);
        }
    } else if (!dead && b.set_absolute_angles)
        b.set_absolute_angles(player, b.view_angles);
}
} // namespace skeet::client
