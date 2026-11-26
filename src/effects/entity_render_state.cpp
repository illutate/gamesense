#include "skeet/effects/entity_render_state.hpp"
#include "skeet/animation/entity_update.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <cstring>

namespace skeet::effects {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
using Vector = lagcomp::Vector;

bool convar_is_zero(void* variable) noexcept {
    for (;;) {
        auto* parent = read<void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return read<std::uint32_t>(variable, 0x30) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable));
}

bool same_vector(const Vector& first, const Vector& second) noexcept {
    return first[0] == second[0] && first[1] == second[1] && first[2] == second[2];
}

void set_angles(void* entity, const Vector& desired, const EntityRenderBindings& bindings) {
    const auto& offsets = bindings.offsets;
    if (!same_vector(read<Vector>(entity, offsets.angles_invalidated), desired)) {
        if (bindings.invalidate_physics)
            bindings.invalidate_physics(entity, 2);
        write(entity, offsets.angles_invalidated, desired);
    }
    write(entity, offsets.angles_copy, desired);
}

void synchronize_ragdoll(void* ragdoll, client::PlayerStateCache& cache,
                         const EntityRenderBindings& bindings) {
    const auto& offsets = bindings.offsets;
    const Vector angles{0.0f, read<float>(ragdoll, offsets.ragdoll_yaw), 0.0f};
    write(ragdoll, offsets.render_flags,
          read<std::uint32_t>(ragdoll, offsets.render_flags) | 0x4810u);
    const auto owner_handle = read<std::uint32_t>(ragdoll, offsets.ragdoll_owner);
    if (owner_handle != UINT32_MAX) {
        auto* owner = engine::invoke<void*>(bindings.entity_list, 4, owner_handle);
        if (owner && engine::invoke<bool>(owner, 158)) {
            const auto index =
                engine::invoke<std::uint32_t>(static_cast<std::byte*>(owner) + 8, 10);
            // As in native 4332631B, this branch requires a real nonzero player
            // index. The engine's IsPlayer predicate establishes that contract.
            if (!index)
                throw std::logic_error("Ragdoll owner requires a nonzero player index");
            auto& state = *cache.get(index);
            if (state.history.count) {
                const auto& snapshot = state.history.at(0).animation;
                animation::restore_entity_animation(snapshot, owner, bindings.animation_offsets);
                animation::restore_entity_animation(snapshot, ragdoll, bindings.animation_offsets);
            }
            // The intermediate array is present in the original, including
            // when the history is empty; the live owner supplies all 24 poses.
            std::array<float, 24> poses;
            std::memcpy(poses.data(),
                        static_cast<const std::byte*>(owner) +
                            bindings.animation_offsets.pose_parameters,
                        sizeof poses);
            std::memcpy(static_cast<std::byte*>(ragdoll) +
                            bindings.animation_offsets.pose_parameters,
                        poses.data(), sizeof poses);
            set_angles(owner, angles, bindings);
        }
    }
    write(ragdoll, offsets.reset_byte_8481ea04, std::uint8_t(0));
    write(ragdoll, offsets.origin_copy, read<Vector>(ragdoll, offsets.origin_source));
    if (!same_vector(read<Vector>(ragdoll, offsets.origin_source),
                     read<Vector>(ragdoll, offsets.origin_invalidated))) {
        if (bindings.invalidate_physics)
            bindings.invalidate_physics(ragdoll, 1);
        // Native reads the original source pointer after invalidation.
        write(ragdoll, offsets.origin_invalidated, read<Vector>(ragdoll, offsets.origin_source));
    }
    set_angles(ragdoll, angles, bindings);
}

void update_entries(client::PlayerStateCache& cache, const EntityRenderBindings& bindings,
                    std::span<const EntityRenderListEntry> entries,
                    const esp::EffectsSettings& settings, const std::uint8_t& weapon_models_enabled,
                    bool zero) {
    const auto& offsets = bindings.offsets;
    for (const auto& entry : entries) {
        if (entry.index <= 64 || (entry.index & 0xFFFF0000u))
            continue;
        auto* entity = engine::invoke<void*>(bindings.entity_list, 3, entry.index);
        if (!entity)
            continue;
        if ((zero || settings.disable_ragdolls.value) &&
            read<void*>(entity, 0) == bindings.ragdoll_vtable) {
            if (settings.disable_ragdolls.value) {
                write(entity, offsets.hidden_byte_1b5f47d5, std::uint8_t(0));
                write(entity, offsets.render_flags,
                      read<std::uint32_t>(entity, offsets.render_flags) | 0x4830u);
                write(entity, offsets.render_mode, std::uint8_t(10));
                write(entity, offsets.reset_byte_8481ea04, std::uint8_t(0));
            } else if (zero) {
                synchronize_ragdoll(entity, cache, bindings);
            }
        }
        if (weapon_models_enabled && engine::invoke<bool>(entity, 159))
            engine::invoke<void>(entity, 238, false);
    }
}

bool begin_update(CameraRuntime& camera, const EntityRenderBindings& bindings,
                  void* observer_player, const esp::EffectsSettings& settings,
                  const std::uint8_t& weapon_models_enabled, bool& zero) {
    camera.update_observer(observer_player);
    zero = convar_is_zero(bindings.ragdoll_convar);
    return zero || settings.disable_ragdolls.value || weapon_models_enabled;
}
}

void update_entity_render_state(CameraRuntime& camera, client::PlayerStateCache& cache,
                                const EntityRenderBindings& bindings, void* observer_player,
                                std::span<const EntityRenderListEntry> entries,
                                const esp::EffectsSettings& settings,
                                const std::uint8_t& weapon_models_enabled) {
    bool zero;
    if (!begin_update(camera, bindings, observer_player, settings, weapon_models_enabled, zero))
        return;
    update_entries(cache, bindings, entries, settings, weapon_models_enabled, zero);
}

void update_entity_render_state_packet(CameraRuntime& camera, client::PlayerStateCache& cache,
                                       const EntityRenderBindings& bindings, void* observer_player,
                                       const void* packet, const esp::EffectsSettings& settings,
                                       const std::uint8_t& weapon_models_enabled) {
    bool zero;
    if (!begin_update(camera, bindings, observer_player, settings, weapon_models_enabled, zero))
        return;
    const auto count = read<std::int32_t>(packet, 0x4044);
    if (count <= 0)
        return;
    const auto* entries = reinterpret_cast<const EntityRenderListEntry*>(
        static_cast<const std::byte*>(packet) + 0x44);
    update_entries(cache, bindings, {entries, static_cast<std::size_t>(count)}, settings,
                   weapon_models_enabled, zero);
}

} // namespace skeet::effects
