#include "skeet/effects/entity_visibility.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/animation/entity_update.hpp"
#include "skeet/esp/player_runtime.hpp"
#include <bit>
#include <cstring>
#include <limits>

namespace skeet::effects {
namespace {
EntityVisibilityRuntime* active_runtime = nullptr;
EntityNetworkableRuntime* active_networkable = nullptr;
engine::VtableHookRegistry* networkable_registry = nullptr;
template <class T> T read(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void* parent(void* variable) {
    for (;;) {
        auto* next = read<void*>(variable, 0x1C);
        if (next == variable)
            return variable;
        variable = next;
    }
}
std::int32_t integer(const void* variable) {
    const auto* root = parent(const_cast<void*>(variable));
    return std::bit_cast<std::int32_t>(read<std::uint32_t>(root, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(root)));
}
void integer(void* variable, std::int32_t value) {
    auto* root = parent(variable);
    const auto encoded =
        std::uint32_t(value) ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(root));
    write(root, 0x30, encoded);
    write(root, 0x40, encoded);
}
void* ragdoll_variable(const EntityNetworkableBindings& bindings) {
    return reinterpret_cast<void*>(std::uintptr_t(bindings.encoded_ragdoll_convar ^ 0x3155EA46u));
}
void render_mode(void* entity, std::uint8_t mode, const EntityNetworkableBindings& bindings) {
    if (read<std::uint8_t>(entity, bindings.offsets.render.render_mode) == mode)
        return;
    write(entity, bindings.offsets.render.render_mode, mode);
    auto* render = engine::invoke<void*>(entity, 9);
    const auto effect = read<std::uint8_t>(entity, bindings.offsets.render_fx);
    engine::invoke<void>(render, 2, std::int32_t(effect), std::int32_t(mode),
                         std::numeric_limits<float>::max(), 0.0f);
}
// Engine adapter for the canonical43359AFA observer resolver. No selected
// entity or relationship answer is supplied by an algorithm callback.
struct EffectivePlayer {
    const EntityNetworkableBindings& bindings;
    esp::RuntimePlayer local{}, observed{};
    esp::PlayerRuntimeEngine engine{};
    explicit EffectivePlayer(const EntityNetworkableBindings& b) : bindings(b) {
        const auto player = *b.visibility.local_player;
        if (player) {
            const auto& offsets = b.visibility.relationships.offsets;
            local.engine_entity = player;
            local.life_state =
                read<std::uint8_t>(player, std::uint32_t(offsets.rejection_bytes[0]));
            local.move_type = read<std::uint8_t>(player, std::uint32_t(offsets.movement_type));
            local.observer_mode = read<std::int32_t>(player, b.offsets.observer_mode);
            local.observer_handle = read<std::int32_t>(player, b.offsets.observer_target);
        }
        engine.context = this;
        engine.local_player = player ? &local : nullptr;
        engine.by_handle = [](void* context, std::int32_t handle) -> esp::RuntimePlayer* {
            auto& self = *static_cast<EffectivePlayer*>(context);
            const auto entity = skeet::engine::invoke<void*>(self.bindings.entity_list, 4, handle);
            if (!entity)
                return nullptr;
            self.observed.engine_entity = entity;
            return &self.observed;
        };
        engine.is_player = [](void*, const esp::RuntimePlayer& player) {
            return skeet::engine::invoke<bool>(player.engine_entity, 158);
        };
    }
    void* selected() {
        const auto* player = esp::effective_view_player(engine);
        return player ? player->engine_entity : nullptr;
    }
};
#if defined(_M_IX86) || defined(__i386__)
std::uint32_t __fastcall render_entry(void* entity, void*, std::uintptr_t first,
                                      std::uintptr_t second) {
#else
std::uint32_t render_entry(void* entity, void*, std::uintptr_t first, std::uintptr_t second) {
#endif
    return filter_teammate_render(active_runtime->settings, active_runtime->bindings, entity, first,
                                  second);
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall networkable_entry(void* receiver, void*, std::int32_t stage) {
#else
void networkable_entry(void* receiver, void*, std::int32_t stage) {
#endif
    active_networkable->post_data_update(receiver, stage);
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall release_networkable_entry(void* receiver, void*) {
#else
void release_networkable_entry(void* receiver, void*) {
#endif
    release_hooked_networkable(*networkable_registry, receiver);
}
}
rage::TargetBindings
EntityVisibilityRelationships::for_player(void* actual_saved_player) const noexcept {
    return {actual_saved_player,         entity_list, game_type.get(),
            teammates_are_enemies.get(), adjustments, offsets};
}
std::uint32_t filter_teammate_render(const esp::EffectsSettings& settings,
                                     const EntityVisibilityBindings& bindings, void* entity,
                                     std::uintptr_t first, std::uintptr_t second) {
    if (settings.disable_teammates.value && !engine::invoke<bool>(bindings.engine, 82) &&
        !engine::invoke<bool>(bindings.engine, 93)) {
        if (void* local = *bindings.local_player) {
            const auto& base = bindings.relationships;
            const auto relation = base.for_player(entity);
            if (!rage::target_is_enemy(relation, local))
                return 0;
        }
    }
    const auto table = *static_cast<void***>(entity);
    const auto metadata = static_cast<engine::VtableHookMetadata*>(table[-2]);
    auto** original_table = metadata->original_table;
    return reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uintptr_t, std::uintptr_t>>(
        original_table[194])(entity, first, second);
}
void EntityVisibilityRuntime::bind_player_table(engine::VtableHookMetadata& metadata) {
    active_runtime = this;
    metadata.replacement_table[194] = source_entry();
}
void* EntityVisibilityRuntime::source_entry() noexcept {
    return reinterpret_cast<void*>(render_entry);
}

void set_entity_fade(void* entity, float start, float duration,
                     const EntityNetworkableBindings& bindings) {
    const bool sample_clock = start == std::numeric_limits<float>::max();
    if (read<std::uint8_t>(entity, bindings.offsets.render_fx) == 15 && sample_clock)
        return;
    if (sample_clock)
        start = read<float>(bindings.globals, 0x10);
    write(entity, bindings.offsets.render_fx, std::uint8_t(15));
    auto* render = engine::invoke<void*>(entity, 9);
    const auto mode = read<std::uint8_t>(entity, bindings.offsets.render.render_mode);
    engine::invoke<void>(render, 2, std::int32_t(15), std::int32_t(mode), start, duration);
}

bool EntityNetworkableRuntime::visible_ragdoll_pass(void* entity) {
    // Complete433E70FB's visible bit, including the real434181CD relationship
    // chain. The local marker precedes every engine/demo/observer operation.
    if (read<std::uint8_t>(entity, bindings.offsets.local_player_flag) & 1)
        return (models.local_player.enabled.value & 1) != 0;
    const auto& base = bindings.visibility.relationships;
    bool teammate;
    if (engine::invoke<bool>(bindings.visibility.engine, 93))
        teammate = read<std::int32_t>(entity, std::uint32_t(base.offsets.team)) == 3;
    else {
        EffectivePlayer view(bindings);
        const auto local = view.selected();
        if (!local)
            teammate = false;
        else if (read<std::int32_t>(local, std::uint32_t(base.offsets.team)) == 1)
            teammate = read<std::int32_t>(entity, std::uint32_t(base.offsets.team)) == 3;
        else {
            const auto relation = base.for_player(local);
            teammate = !rage::target_is_enemy(relation, entity);
        }
    }
    const auto& group = teammate ? models.teammate : models.player;
    std::uint32_t flags = ((group.enabled.value & 1u) << 2) | ((group.behind_wall.value & 1u) << 3);
    if (!teammate && models.shadow.value &&
        integer(reinterpret_cast<void*>(
            std::uintptr_t(bindings.encoded_shadow_history_convar ^ 0x3AE06513u))) != 0)
        flags |= 16;
    return (flags & 4) != 0;
}

void EntityNetworkableRuntime::post_data_update(void* receiver, std::int32_t stage) {
    auto* entity = static_cast<std::byte*>(receiver) - 8;
    const auto& fields = bindings.offsets.render;
    const bool restore_convar = settings.disable_ragdolls.value != 0;
    std::int32_t saved_convar = 0;
    if (restore_convar) {
        saved_convar = integer(ragdoll_variable(bindings));
        integer(ragdoll_variable(bindings), 0);
    }
    auto owner = [&]() -> void* {
        const auto handle = read<std::uint32_t>(entity, fields.ragdoll_owner);
        return handle == UINT32_MAX ? nullptr
                                    : engine::invoke<void*>(bindings.entity_list, 4, handle);
    };
    if (stage == 0 && models.ragdolls.value)
        if (auto* player = owner(); player && visible_ragdoll_pass(player))
            engine::invoke<void>(entity, 238, false);
    bool hidden = settings.disable_ragdolls.value != 0;
    if (!hidden && settings.disable_teammates.value &&
        !engine::invoke<bool>(bindings.visibility.engine, 82) &&
        !engine::invoke<bool>(bindings.visibility.engine, 93)) {
        if (auto* player = owner()) {
            auto* local = *bindings.visibility.local_player;
            const auto& base = bindings.visibility.relationships;
            if (local &&
                read<std::uint8_t>(local, std::uint32_t(base.offsets.rejection_bytes[0])) == 0) {
                const auto relation = base.for_player(player);
                if (!rage::target_is_enemy(relation, local))
                    hidden = true;
            }
        }
    }
    if (stage == 2 && !hidden) {
        if (integer(ragdoll_variable(bindings)) == 0) {
            if (auto* player = owner(); player && engine::invoke<bool>(player, 158)) {
                const auto index =
                    engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
                auto& state = *players.get(index);
                if (state.history.count)
                    animation::restore_entity_animation(state.history.at(0).animation, player,
                                                        bindings.animation_offsets);
                else {
                    std::array<float, 24> poses;
                    std::memcpy(poses.data(),
                                static_cast<const std::byte*>(player) +
                                    bindings.animation_offsets.pose_parameters,
                                sizeof poses);
                    std::memcpy(entity + bindings.animation_offsets.pose_parameters, poses.data(),
                                sizeof poses);
                }
            }
            const auto source = read<lagcomp::Vector>(entity, fields.origin_source);
            const auto destination = read<lagcomp::Vector>(entity, fields.origin_invalidated);
            if (source[0] != destination[0] || source[1] != destination[1] ||
                source[2] != destination[2]) {
                if (bindings.invalidate_physics)
                    bindings.invalidate_physics(entity, 1);
                write(entity, fields.origin_invalidated,
                      read<lagcomp::Vector>(entity, fields.origin_source));
            }
        }
    } else if (hidden) {
        write(entity, fields.hidden_byte_1b5f47d5, std::uint8_t(0));
        write(entity, fields.reset_byte_8481ea04, std::uint8_t(0));
        write(entity, fields.render_flags,
              read<std::uint32_t>(entity, fields.render_flags) | 0x4830u);
        render_mode(entity, 10, bindings);
    }
    const auto table = *static_cast<void***>(receiver);
    const auto* metadata = static_cast<engine::VtableHookMetadata*>(table[-2]);
    reinterpret_cast<engine::MemberFunction<void, std::int32_t>>(metadata->original_table[5])(
        receiver, stage);
    if (stage == 2) {
        if (hidden || integer(ragdoll_variable(bindings)) == 0) {
            if (!hidden) {
                write(entity, fields.render_flags,
                      read<std::uint32_t>(entity, fields.render_flags) | 0x4810u);
                if (bindings.invalidate_render_flags)
                    bindings.invalidate_render_flags(entity);
            }
            const auto* children = read<const std::uint32_t*>(entity, fields.ragdoll_yaw + 0xC);
            const auto count = read<std::uint32_t>(entity, fields.ragdoll_yaw + 0x18);
            const auto* end = children + count;
            for (; children != end; ++children) {
                if (*children == UINT32_MAX)
                    continue;
                auto* child = engine::invoke<void*>(bindings.entity_list, 4, *children);
                if (!child)
                    continue;
                write(child, fields.render_flags,
                      read<std::uint32_t>(child, fields.render_flags) |
                          (hidden ? 0x4830u : 0x4810u));
                render_mode(child, hidden ? 10 : 2, bindings);
                if (!hidden)
                    set_entity_fade(child, read<float>(bindings.globals, 0x10), 0.6f, bindings);
                if (bindings.invalidate_render_flags)
                    bindings.invalidate_render_flags(child);
            }
        }
    }
    if (restore_convar)
        integer(ragdoll_variable(bindings), saved_convar);
}
void EntityNetworkableRuntime::bind_networkable_table(engine::VtableHookMetadata& metadata) {
    active_networkable = this;
    networkable_registry = &bindings.hook_registry;
    bind_static_networkable_table(metadata);
}
void EntityNetworkableRuntime::bind_static_networkable_table(engine::VtableHookMetadata& metadata) {
    metadata.replacement_table[1] = reinterpret_cast<void*>(release_networkable_entry);
    metadata.replacement_table[5] = reinterpret_cast<void*>(networkable_entry);
}
void release_hooked_networkable(engine::VtableHookRegistry& registry, void* receiver) {
    engine::uninstall_vtable_hook(registry, receiver);
    engine::invoke<void>(receiver, 1);
}
} // namespace skeet::effects
