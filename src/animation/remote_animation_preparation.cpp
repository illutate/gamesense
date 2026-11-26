#include "skeet/animation/remote_animation_preparation.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cmath>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, T value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void* from_handle(void* entities, std::uint32_t handle) {
    return handle == 0xffffffffu ? nullptr : engine::invoke<void*>(entities, 4, handle);
}
}
RemoteAnimationPreparationFields::RemoteAnimationPreparationFields(
    const engine::NativeOffsetGlobals& data)
    : frame(data, 0x43467BA4, 0x43468E1C, 0x74019D87, 0),
      ik_mask(data, 0x43467CD8, 0x43468DD8, 0x01D0BACC, 0),
      flags(data, 0x43467634, 0x43468F2C, 0xBB78DC43, 0),
      previous_flags(data, 0x43468ED0, 0x4346A394, 0xABD16CE2, 0),
      active_weapon(data, 0x43467A74, 0x434678C0, 0xCD812654, 0),
      animation_state(data, 0x43467F4C, 0x4346A8B0, 0x3D52A1A7, 0),
      view_gate(data, 0x43468BF8, 0x43467B8C, 0xD070684C, 0),
      observer_mode(data, 0x4346A6CC, 0x43468EF8, 0x2F4E97C4, 0),
      observer_target(data, 0x43468F84, 0x43467E64, 0x3B704B41, 0),
      world_model(data, 0x43467180, 0x43467FA4, 0xCA2B5FFB, 0),
      eye_origin(data, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0),
      duck_amount(data, 0x434679F8, 0x434675F8, 0x3309C0CF, 0) {}

bool RemoteAnimationPreparationRuntime::allows_view_animation(void* player) const {
    //433BFF50's opaque arithmetic selects player slot363 and input slot32.
    return engine::invoke<std::int32_t>(player, 363, 0) == 2 ||
           engine::invoke<std::int32_t>(bindings.input, 32, -1) != 0;
}
void RemoteAnimationPreparationRuntime::prepare(void* player) const {
    const auto& b = bindings;
    if (engine::invoke<bool>(b.engine, 93))
        return;
    const auto& f = b.fields;
    if (b.frame == read<std::int32_t>(player, f.frame))
        return;
    write(player, f.ik_mask, 0xffffffffu);
    bool force_animation = read<std::int32_t>(player, f.frame) == 0;
    auto* const weapon = from_handle(b.entity_list, read<std::uint32_t>(player, f.active_weapon));
    auto* const animation = read<void*>(player, f.animation_state);
    if (animation && weapon && read<void*>(animation, 0x5c) != weapon) {
        force_animation = true;
        write(animation, 0x5c, weapon);
    }
    write(player, f.frame, b.frame);
    write(player, f.previous_flags, read<std::uint32_t>(player, f.flags));
    write(player, f.flags, 4u);

    auto* const networkable = static_cast<std::byte*>(player) + 8;
    auto* const renderable = static_cast<std::byte*>(player) + 4;
    if (!force_animation && !engine::invoke<bool>(networkable, 9) &&
        engine::invoke<bool>(renderable, 3) &&
        (!read<std::uint8_t>(player, f.view_gate) || allows_view_animation(player))) {
        auto* const local = b.local_player;
        const bool observing =
            local && read<std::int32_t>(local, f.observer_mode) == 4 &&
            from_handle(b.entity_list, read<std::uint32_t>(local, f.observer_target)) == player;
        if (!observing) {
            const auto eye = rage::target_eye_position(
                rage::TargetEyePositionOffsets{f.eye_origin, f.duck_amount}, player);
            const float x = eye[0] - b.view_origin[0];
            const float y = eye[1] - b.view_origin[1];
            const float z = eye[2] - b.view_origin[2];
            const float distance_squared = (x * x + y * y) + z * z;
            if (distance_squared > 160000.0f) {
                const float distance = std::sqrt(distance_squared);
                if (distance > 400.0f) {
                    write(player, f.flags, read<std::uint32_t>(player, f.flags) | 1u);
                    constexpr float radians = 0.01745329238474369f;
                    const float yaw = b.view_angles[1] * radians;
                    const float yaw_sine = b.trigonometry.sine(yaw);
                    const float yaw_cosine = b.trigonometry.cosine(yaw);
                    const float pitch = b.view_angles[0] * radians;
                    const float pitch_sine = b.trigonometry.sine(pitch);
                    const float pitch_cosine = b.trigonometry.cosine(pitch);
                    const float forward_x = pitch_cosine * yaw_cosine;
                    const float forward_y = pitch_cosine * yaw_sine;
                    const float forward_z = -pitch_sine;
                    constexpr float epsilon = 0x1p-23f;
                    const float inverse_forward =
                        1.0f / (std::sqrt((forward_x * forward_x + forward_y * forward_y) +
                                          forward_z * forward_z) +
                                epsilon);
                    const float inverse_distance = 1.0f / (distance + epsilon);
                    const float cosine = ((y * inverse_distance) * (inverse_forward * forward_y) +
                                          (x * inverse_distance) * (inverse_forward * forward_x)) +
                                         (z * inverse_distance) * (inverse_forward * forward_z);
                    // COMISS/JAE falls through for unordered operands as well.
                    if (!(cosine >= -0.258819043636322f))
                        write(player, f.flags, read<std::uint32_t>(player, f.flags) | 2u);
                }
            }
        }
    }

    if (weapon) {
        if (auto* const world =
                from_handle(b.entity_list, read<std::uint32_t>(weapon, f.world_model))) {
            write(world, f.flags, read<std::uint32_t>(player, f.flags));
            write(world, f.previous_flags, read<std::uint32_t>(player, f.previous_flags));
        }
    }
    if (!force_animation) {
        const auto flags = read<std::uint32_t>(player, f.flags);
        if ((flags & 1u) && !((read<std::uint32_t>(player, f.previous_flags) ^ flags) & 1u))
            write(player, f.ik_mask, 0x300u);
        if (!engine::invoke<bool>(networkable, 9) ||
            (read<std::uint32_t>(player, f.previous_flags) & 8u))
            return;
    }
    write(player, f.ik_mask, 0xffffffffu);
}
} // namespace skeet::animation
