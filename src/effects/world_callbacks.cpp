#include "skeet/effects/world_callbacks.hpp"
#include "skeet/engine/interface_call.hpp"
#include <atomic>
#include <cstring>

namespace skeet::effects {
namespace {
template <class T> T read(const void* p, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof value);
    return value;
}
template <class T> void write(void* p, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof value);
}
void exchange(ModelHookDescriptor& hook, void** table) {
    std::atomic_ref<void**>(*hook.object).exchange(table, std::memory_order_seq_cst);
}
bool camera_player(void* player, const CameraNetvars& offsets) {
    if (!read<std::uint8_t>(player, offsets.life_state))
        return true;
    const auto mode = read<std::int32_t>(player, offsets.observer_mode);
    return mode == 1 || mode == 2;
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_WORLD_PHASE __thiscall
#define SKEET_WORLD_FAST __fastcall
#else
#define SKEET_WORLD_PHASE
#define SKEET_WORLD_FAST
#endif
void SKEET_WORLD_PHASE begin(void* receiver) {
    static_cast<WorldUpdateObserver*>(receiver)->runtime->begin();
}
void SKEET_WORLD_PHASE end(void* receiver) {
    static_cast<WorldUpdateObserver*>(receiver)->runtime->end();
}
void SKEET_WORLD_PHASE clear(void* receiver) {
    static_cast<WorldUpdateObserver*>(receiver)->runtime->clear();
}
void SKEET_WORLD_PHASE empty(void*) {} //actual433CCE8E
void SKEET_WORLD_PHASE prepare(void* receiver) {
    static_cast<WorldUpdateObserver*>(receiver)->runtime->prepare();
}
void SKEET_WORLD_FAST empty_player_view(void*, void*, std::uintptr_t, std::uintptr_t,
                                        std::uintptr_t) {} //4337D653
void SKEET_WORLD_FAST view(void* receiver, float*, float*, float*) {
    auto& r = *static_cast<WorldUpdateObserver*>(receiver)->runtime;
    update_shot_entity_bones(r.world.interfaces.entity_list, r.bindings.shot_entity_handle,
                             r.bindings.offsets.bones, r.bindings.bone_counter);
}
void SKEET_WORLD_FAST post_view(void* receiver, void* player) {
    auto& r = *static_cast<WorldUpdateObserver*>(receiver)->runtime;
    r.models.update_local_transparency(
        player, player ? static_cast<std::byte*>(player) + 4 : nullptr,
        r.bindings.transparency_interfaces, r.bindings.transparency_offsets, r.world.suspended != 0,
        r.bindings.player_effects != 0, r.bindings.frame_time);
}
const network::UpdateObserverVtable vtable{
    {begin, end, clear, empty, prepare}, empty_player_view, view, post_view};
#undef SKEET_WORLD_PHASE
#undef SKEET_WORLD_FAST
}
WorldUpdateObserver::WorldUpdateObserver(WorldCallbacks& runtime) : endpoint{}, runtime(&runtime) {}
void WorldUpdateObserver::initialize_vtable() noexcept {
    endpoint.vtable = &vtable;
}
WorldUpdateObserver::WorldUpdateObserver(engine::OwnerRegistryBinding registry,
                                         WorldCallbacks& runtime)
    : WorldUpdateObserver(runtime) {
    network::register_update_observer(registry, endpoint);
    initialize_vtable();
}
void WorldCallbacks::begin() {
    if (bindings.install_second_hook) {
        if (!state.second_hook_installed) {
            state.second_hook_installed = 1;
            exchange(bindings.second_hook, bindings.second_hook.replacement_table);
        }
    } else if (state.second_hook_installed) {
        state.second_hook_installed = 0;
        if (bindings.second_hook.object)
            exchange(bindings.second_hook, bindings.second_hook.original_table);
    }
    exchange(bindings.first_hook, bindings.first_hook.replacement_table);
    if (world.suspended)
        return;
    const auto index = engine::invoke<std::int32_t>(world.interfaces.engine, 12);
    void* local = engine::invoke<void*>(world.interfaces.entity_list, 3, index);
    if (!local)
        return;
    if (state.camera_pending) {
        engine::invoke<void>(local, 312, true);
        state.camera_pending = 0;
    }
    world.begin_render_for_player(local);
}
void WorldCallbacks::end() {
    if (state.saved_sky_name && bindings.sky_name_variable) {
        if (read<void*>(bindings.sky_name_variable, 0x24) == lifecycle.allocation_760)
            write(bindings.sky_name_variable, 0x24, state.saved_sky_name);
        else {
            lifecycle.allocation_760 = nullptr;
            engine::invoke<void>(bindings.allocator, 5, state.saved_sky_name);
        }
        state.saved_sky_name = nullptr;
    }
    if (bindings.first_hook.object)
        exchange(bindings.first_hook, bindings.first_hook.original_table);
    world.end_render();
}
void WorldCallbacks::clear() {
    auto* variable = bindings.clear_convar.get();
    for (;;) {
        auto* parent = read<void*>(variable, 0x1c);
        if (parent == variable)
            break;
        variable = parent;
    }
    const auto encoded = std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
    write(variable, 0x30, encoded);
    write(variable, 0x40, encoded);
}
void WorldCallbacks::prepare() {
    if (world.settings.brightness.value == 2 && bindings.sky_name_variable) {
        if (!lifecycle.allocation_760) {
            lifecycle.allocation_760 =
                engine::invoke<void*>(bindings.allocator, 1, std::uint32_t(17));
            if (lifecycle.allocation_760) {
                // Constructor4348EFEB..4348F03C decrypts these16 bytes and
                // appends the seventeenth null byte through433B2213.
                std::memcpy(lifecycle.allocation_760, state.sky_name.data(), 17);
            }
        }
        if (lifecycle.allocation_760) {
            state.saved_sky_name = read<void*>(bindings.sky_name_variable, 0x24);
            write(bindings.sky_name_variable, 0x24, lifecycle.allocation_760);
        }
    }
    if (bindings.shot_entity_handle != UINT32_MAX) {
        if (void* marker = engine::invoke<void*>(world.interfaces.entity_list, 4,
                                                 bindings.shot_entity_handle)) {
            void* renderable = static_cast<std::byte*>(marker) + 4;
            if (*engine::invoke<const std::uint16_t*>(renderable, 7) != UINT16_MAX) {
                auto* manager = bindings.model_manager;
                const auto handle = *engine::invoke<const std::uint16_t*>(renderable, 7);
                engine::invoke<void>(manager, 3, std::uint32_t(handle), true);
            }
            update_shot_entity_bones_for_entity(marker, bindings.offsets.bones,
                                                bindings.bone_counter);
            const auto duration = double(float(models.settings.on_shot_duration.value)) * 0.1;
            const auto now = engine::invoke<double>(models.interfaces.clock, 5);
            const auto remaining = bindings.shot.expires - now;
            if (!models.settings.on_shot.enabled.value || remaining <= 0.0 ||
                remaining > duration || bindings.shot.bones_begin == bindings.shot.bones_end) {
                if (bindings.shot_entity_handle != UINT32_MAX) {
                    if (void* current = engine::invoke<void*>(world.interfaces.entity_list, 4,
                                                              bindings.shot_entity_handle)) {
                        auto flags = read<std::uint32_t>(current, bindings.offsets.render_flags);
                        if (!(flags & 0x20u)) {
                            write(current, bindings.offsets.render_flags, flags | 0x20u);
                            if (bindings.invalidate_render_flags)
                                bindings.invalidate_render_flags(current);
                        }
                    }
                }
            }
        }
    }
    auto* local = bindings.camera.local_player;
    if (!local)
        return;
    if (!world.suspended) {
        if (models.settings.weapon_viewmodel.enabled.value) {
            auto* handles = static_cast<std::byte*>(local) + bindings.offsets.inventory;
            for (std::uint32_t i = 0; i < 48; ++i) {
                const auto handle = read<std::uint32_t>(handles, i * 4);
                if (handle == UINT32_MAX)
                    continue;
                auto* weapon = engine::invoke<void*>(world.interfaces.entity_list, 4, handle);
                if (!weapon)
                    continue;
                const auto attached =
                    read<std::uint32_t>(weapon, bindings.offsets.weapon_world_model);
                if (attached != UINT32_MAX)
                    if (auto* model =
                            engine::invoke<void*>(world.interfaces.entity_list, 4, attached))
                        engine::invoke<void>(model, 238, false);
                engine::invoke<void>(weapon, 238, false);
            }
            if (world.suspended)
                goto restore_camera;
        }
        view.instant_scope(local);
        if (world.suspended)
            goto restore_camera;
        if (world.settings.third_person_alive.value &&
            hotkey_active(world.settings.third_person_key.value.binding, bindings.input) &&
            read<std::int32_t>(local, bindings.camera_offsets.fov) != 45 &&
            camera_player(local, bindings.camera_offsets)) {
            write(bindings.camera.input, 0xa9, std::uint8_t(0));
            write(bindings.camera.input, 0x100, std::int32_t(1));
            world.material_flags |= 0x10;
            state.camera_pending = 1;
            engine::invoke<void>(bindings.camera_snap_to.get(), 16, true);
            return;
        }
    }
restore_camera:
    if (world.material_flags & 0x10) {
        engine::invoke<void>(bindings.camera.input, 36);
        engine::invoke<void>(local, 312, false);
        world.material_flags &= 0xef;
    }
}
} // namespace skeet::effects
