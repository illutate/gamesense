#include "skeet/skins/gloves.hpp"
#include <bit>

namespace skeet::skins {
namespace {
constexpr float glove_wear = std::bit_cast<float>(std::uint32_t(0x3727C5AC));
std::uint32_t entity_index(std::uint32_t handle) {
    return handle == invalid_glove_handle ? 0x1FFF : handle & 0xFFFF;
}
GloveEntity* resolve(GloveEngine& engine, std::uint32_t handle) {
    return handle == invalid_glove_handle ? nullptr : engine.resolve(handle);
}
// 0x43364942. The two registration operations are resolved engine functions;
// the factory, model initialization, index restoration and teardown are retained.
GloveEntity* create_glove(GloveFactory& factory, const char* model, GloveEngine& engine,
                          ModelInfo& models) {
    const auto old_highest = engine.highest_entity_index();
    auto* created = factory.create(0x7FE, 0x7FF);
    if (!created)
        return nullptr;
    if (engine.entity_registration_available()) {
        auto* entity = created->unknown()->base_entity();
        engine.register_entity(entity_index(entity->handle));
        engine.highest_entity_index() = old_highest;
        if (models.model_index(model) != -1 && engine.initialize_model(*entity, model, false))
            entity->creation_index = std::int32_t(entity_index(entity->handle));
        else {
            auto handle = invalid_glove_handle;
            engine.unregister_entity(handle, *entity);
        }
    }
    created->notify_transmission(0);
    return created->unknown()->base_entity();
}
// 0x433E320A: fields 43467F9C/43468C1C,4346A8A8,43468950,
// 4346908C and 4346A358; model selection crosses the real MDL cache calls.
void set_glove(GloveEntity& entity, std::int32_t paint, std::uint32_t definition,
               std::int32_t model, GloveEngine& engine) {
    entity.item_id_low = -1;
    entity.item_id_high = -1;
    entity.paintkit = paint;
    entity.wear = glove_wear;
    entity.definition = std::uint16_t(definition);
    entity.account_id = engine.local_account_id();
    engine.begin_model_cache();
    engine.set_model_index(entity, model);
    engine.end_model_cache();
}
// 0x433E2266 only updates attributes already present; it does not append one.
void set_attribute(GloveEntity& entity, std::uint32_t definition, float value,
                   GloveEngine& engine) {
    for (auto& attribute : entity.attributes) {
        if (attribute.definition != definition)
            continue;
        if (attribute.value == value && attribute.initial_value == value)
            return;
        attribute.value = value;
        attribute.initial_value = value;
        if (entity.attribute_cache) {
            engine.release_attribute_cache(entity.attribute_cache, true);
            entity.attribute_cache = nullptr;
        }
        return;
    }
}
bool matches(const GloveEntity& entity, std::int32_t paint, std::uint32_t definition,
             std::int32_t model) {
    return entity.paintkit == paint && std::uint32_t(entity.definition) == definition &&
           entity.model_index == model;
}
void detach(GloveEntity& entity, GloveEntity& local, GloveEngine& engine) {
    engine.equip(entity, local, false);
    engine.unequip(entity, local);
}
void destroy(GloveEntity* entity, bool recreating) {
    if (!entity || !entity->networkable)
        return;
    if (recreating)
        entity->networkable->mark_destroyed_on_recreate();
    entity->networkable->notify_transmission(1);
    entity->networkable->release();
}
// 0x43342DD7: select the player's indexed view model, following a valid live
// observer target in mode four. The original routine also uses this recursion.
GloveEntity* view_model(GloveEntity& player, std::int32_t index, GloveEngine& engine) {
    auto* result = resolve(engine, player.view_models[std::size_t(index)]);
    if (player.observer_mode != 4)
        return result;
    auto* target = resolve(engine, player.observer_target);
    if (target && target != &player && !target->dead && engine.health(*target) > 0 &&
        !engine.network_dormant(*target) && !target->observer_excluded)
        result = view_model(*target, index, engine);
    return result;
}
void teardown(GloveOwnerState& state, GloveEntity* local, GloveEngine& engine) {
    if (state.handle == invalid_glove_handle)
        return;
    if (local && local->wearable_handle != invalid_glove_handle &&
        local->wearable_handle == state.handle) {
        if (auto* worn = resolve(engine, local->wearable_handle)) {
            detach(*worn, *local, engine);
            local->wearable_change_counter = 0;
        }
        local->wearable_handle = invalid_glove_handle;
    }
    destroy(resolve(engine, state.handle), false);
    state.handle = invalid_glove_handle;
}
}
void apply_gloves(const Settings& settings, GloveOwnerState& state, GloveEntity* local,
                  GloveEngine& engine, SchemaEngine& schema, ModelInfo& models) {
    if (!local || engine.demo_playback() || engine.hltv()) {
        teardown(state, local, engine);
        return;
    }
    const bool ct = local->team == 3;
    if (!(ct ? settings.glove_ct.value : settings.glove_t.value) || local->dormant) {
        teardown(state, local, engine);
        return;
    }
    if (local->dead)
        return;
    const auto definition =
        std::uint32_t(ct ? settings.glove_model_ct.value : settings.glove_model_t.value);
    if (!schema.acquire_schema())
        return;
    const auto item = schema.definition(definition);
    if (!item || !item->world_model || !item->view_model)
        return;
    const auto world = models.model_index(item->world_model);
    const auto view = models.model_index(item->view_model);
    if (world == -1 || view == -1 || !state.factory)
        return;
    const auto paint = ct ? settings.glove_paint_ct.value : settings.glove_paint_t.value;
    if (resolve(engine, local->wearable_handle) && !resolve(engine, state.handle)) {
        if (auto* worn = resolve(engine, local->wearable_handle)) {
            if (!matches(*worn, paint, definition, world))
                set_glove(*worn, paint, definition, world, engine);
            set_attribute(*worn, 6, float(paint), engine);
            set_attribute(*worn, 8, glove_wear, engine);
            return;
        }
    }
    auto* owned = resolve(engine, state.handle);
    if (owned) {
        if (!owned->networkable || owned->networkable->client_class() != state.factory)
            return;
        if (matches(*owned, paint, definition, world) && local->wearable_handle == state.handle) {
            if (local->transmission_state != 1)
                engine.equip(*owned, *local, true);
            return;
        }
        if (local->wearable_handle != invalid_glove_handle) {
            if (auto* current = resolve(engine, local->wearable_handle)) {
                detach(*current, *local, engine);
                local->wearable_change_counter = 0;
            }
            local->wearable_handle = invalid_glove_handle;
        }
        destroy(resolve(engine, state.handle), true);
    }
    state.handle = invalid_glove_handle;
    auto* created = create_glove(*state.factory, item->world_model, engine, models);
    if (!created)
        return;
    state.handle = created->handle;
    auto& override_local = engine.local_override();
    auto* old_override = override_local;
    const bool set_override = override_local == nullptr;
    if (set_override)
        override_local = local;
    created->flags |= 0x80;
    created->quality = 11;
    set_glove(*created, paint, definition, world, engine);
    const auto old_model_update = local->model_update_handle;
    local->model_update_handle = invalid_glove_handle;
    engine.set_model_instance(*created, engine.model_instance(*created));
    local->model_update_handle = old_model_update;
    if (auto* current = resolve(engine, local->wearable_handle))
        detach(*current, *local, engine);
    local->wearable_handle = invalid_glove_handle;
    engine.attach(*created, *local);
    engine.equip(*created, *local, true);
    created->owner_handle = local->handle;
    engine.post_equip(*created);
    if (auto* weapon = resolve(engine, local->active_weapon_handle)) {
        if (auto* model = view_model(*local, weapon->view_model_index, engine)) {
            engine.invalidate_bone_cache(*model);
            engine.invalidate_physics(*model, 0x20);
        }
    }
    created->initialized = true;
    if (created->networkable)
        created->networkable->on_data_changed(0);
    local->wearable_change_counter = 0;
    if (set_override)
        override_local = old_override;
}
} // namespace skeet::skins
