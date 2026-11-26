#include "skeet/effects/model_render_lifecycle.hpp"
#include "skeet/engine/interface_call.hpp"
#include <atomic>
#include <cstring>

namespace skeet::effects {
namespace {
void restore_hook(ModelHookDescriptor& hook) {
    if (hook.replacement_table && hook.object)
        std::atomic_ref<void**>(*hook.object)
            .exchange(hook.original_table, std::memory_order_seq_cst);
}
void release_material(void*& material) {
    if (material) {
        engine::invoke<void>(material, 13);
        material = nullptr;
    }
}
void release_texture(void*& texture) {
    if (texture) {
        engine::invoke<void>(texture, 11);
        texture = nullptr;
    }
}
}
void ModelRenderLifecycle::initialize_bindings() {
    bindings.camera.observer_proxy = bindings.initial_observer_proxy;
    if (bindings.first_object) {
        bindings.first_hook.object = static_cast<void***>(bindings.first_object);
        bindings.first_hook.owner = &models;
    }
    if (bindings.second_object) {
        bindings.second_hook.object = static_cast<void***>(bindings.second_object);
        bindings.second_hook.owner = &models;
    }
}
void ModelRenderLifecycle::reset() {
    state.field_450 = 0;
    world.material_flags = 0;
    world.previous_prop_transparency = 100;
    for (auto& material : models.materials) {
        material.applied_color = UINT32_MAX;
        material.applied_kind = -1;
    }
    bindings.shot_entity_handle = UINT32_MAX;
}
void ModelRenderLifecycle::reset_thunk() {
    reset();
}
void ModelRenderLifecycle::clear_shot_marker() {
    bindings.shot_entity_handle = UINT32_MAX;
}
void update_shot_entity_bones(void* entity_list, std::uint32_t handle,
                              ShotEntityBoneOffsets offsets, const std::uint32_t* current_counter) {
    if (handle == UINT32_MAX)
        return;
    if (void* entity = engine::invoke<void*>(entity_list, 4, handle))
        update_shot_entity_bones_for_entity(entity, offsets, current_counter);
}
void update_shot_entity_bones_for_entity(void* entity, ShotEntityBoneOffsets offsets,
                                         const std::uint32_t* current_counter) {
    const std::uint32_t mask = 0xFFF00;
    auto* bytes = static_cast<std::byte*>(entity);
    std::memcpy(bytes + offsets.bone_accessor + 8, &mask, 4);
    std::memcpy(bytes + offsets.bone_accessor + 12, &mask, 4);
    if (current_counter)
        std::memcpy(bytes + offsets.recent_counter, current_counter, 4);
}
void ModelRenderLifecycle::shutdown() {
    world.post_processing.restore();
    restore_hook(bindings.first_hook);
    restore_hook(bindings.second_hook);
    auto& effects = world.settings;
    effects.brightness.value = 0;
    effects.wall_transparency.value = 100;
    colors.props.value = 0;
    effects.prop_transparency.value = 100;
    effects.third_person_alive.value = 0;
    colors.player.enabled.value = 0;
    colors.player.behind_wall.value = 0;
    colors.hands.enabled.value = 0;
    colors.teammate.enabled.value = 0;
    colors.teammate.behind_wall.value = 0;
    colors.local_player.enabled.value = 0;
    colors.shadow.value = 0;
    colors.on_shot.enabled.value = 0;
    colors.weapon_viewmodel.enabled.value = 0;
    effects.disable_ragdolls.value = 0;
    if (engine::invoke<bool>(bindings.camera.engine, 26)) {
        if (engine::invoke<void*>(bindings.camera.input, 32, std::int32_t(-1))) {
            engine::invoke<void>(bindings.camera.input, 36);
            if (bindings.camera.local_player)
                engine::invoke<void>(bindings.camera.local_player, 312, false);
        }
        world.update();
    }
    for (auto& material : models.materials)
        release_material(material.material);
    for (void** reference : std::array<void**, 2>{&models.solid_material, &models.shadow_material})
        if (models.interfaces.material_system)
            release_material(*reference);
    for (void** reference :
         std::array<void**, 4>{&models.base_texture, &models.metallic_environment,
                               &models.glow_environment, &models.fullscreen_texture})
        if (models.interfaces.material_system)
            release_texture(*reference);
    if (bindings.shot_entity_handle != UINT32_MAX) {
        if (void* marker = engine::invoke<void*>(bindings.camera.entity_list, 4,
                                                 bindings.shot_entity_handle)) {
            bindings.shot_entity_handle = UINT32_MAX;
            engine::invoke<void>(static_cast<std::byte*>(marker) + 8, 1);
        }
    }
    if (state.allocation_760) {
        engine::invoke<void>(bindings.allocator, 5, state.allocation_760);
        state.allocation_760 = nullptr;
    }
}
} // namespace skeet::effects
