#include "skeet/skins/masks.hpp"

namespace skeet::skins {
namespace {
constexpr std::array<std::string_view, 22> paths{{
    "models/player/holiday/facemasks/facemask_hoxton.mdl",
    "models/player/holiday/facemasks/porcelain_doll.mdl",
    "models/player/holiday/facemasks/facemask_skull.mdl",
    "models/player/holiday/facemasks/facemask_samurai.mdl",
    "models/player/holiday/facemasks/evil_clown.mdl",
    "models/player/holiday/facemasks/facemask_wolf.mdl",
    "models/player/holiday/facemasks/facemask_sheep_model.mdl",
    "models/player/holiday/facemasks/facemask_bunny_gold.mdl",
    "models/player/holiday/facemasks/facemask_anaglyph.mdl",
    "models/player/holiday/facemasks/facemask_porcelain_doll_kabuki.mdl",
    "models/player/holiday/facemasks/facemask_dallas.mdl",
    "models/player/holiday/facemasks/facemask_pumpkin.mdl",
    "models/player/holiday/facemasks/facemask_sheep_bloody.mdl",
    "models/player/holiday/facemasks/facemask_devil_plastic.mdl",
    "models/player/holiday/facemasks/facemask_boar.mdl",
    "models/player/holiday/facemasks/facemask_chains.mdl",
    "models/player/holiday/facemasks/facemask_tiki.mdl",
    "models/player/holiday/facemasks/facemask_bunny.mdl",
    "models/player/holiday/facemasks/facemask_sheep_gold.mdl",
    "models/player/holiday/facemasks/facemask_zombie_fortune_plastic.mdl",
    "models/player/holiday/facemasks/facemask_chicken.mdl",
    "models/player/holiday/facemasks/facemask_skull_gold.mdl",
}};
MaskEntity* resolve(MaskEngine& engine, std::uint32_t handle) {
    return handle == invalid_glove_handle ? nullptr : engine.resolve(handle);
}
std::uint32_t entity_index(std::uint32_t handle) {
    return handle == invalid_glove_handle ? 0x1FFF : handle & 0xFFFF;
}
// 0x43364942, using the existing real factory/networkable/model primitives.
MaskEntity* create_mask(GloveFactory& factory, const char* model, MaskEngine& engine,
                        ModelInfo& models) {
    const auto old_highest = engine.highest_entity_index();
    auto* created = factory.create(0x7FE, 0x7FF);
    if (!created)
        return nullptr;
    if (engine.entity_registration_available()) {
        auto* entity = static_cast<MaskEntity*>(created->unknown()->base_entity());
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
    return static_cast<MaskEntity*>(created->unknown()->base_entity());
}
void zero_transform(MaskEntity& mask, std::array<float, 3>& value, std::int32_t flags,
                    MaskEngine& engine) {
    if (value[0] != 0.0f || value[1] != 0.0f || value[2] != 0.0f) {
        engine.invalidate_transform(mask, flags);
        value = {0.0f, 0.0f, 0.0f};
    }
}
void teardown(MaskOwnerState& state, MaskEngine& engine) {
    if (auto* entity = resolve(engine, state.handle)) {
        if (auto* networkable = entity->networkable) {
            networkable->notify_transmission(1);
            networkable->release();
        }
    }
    state.handle = invalid_glove_handle;
}
}
std::span<const std::string_view, 22> mask_model_paths() noexcept {
    return paths;
}
void apply_mask(const Settings& settings, MaskOwnerState& state, MaskEntity* local,
                MaskEngine& engine, ModelInfo& models, StringTables& tables) {
    // Native 0x4340884C gates even teardown on availability of SetParent.
    if (!engine.parent_function_available())
        return;
    if (!local || engine.demo_playback() || engine.hltv()) {
        teardown(state, engine);
        return;
    }
    const bool ct = local->team == 3;
    if (!(ct ? settings.mask_ct.value : settings.mask_t.value)) {
        teardown(state, engine);
        return;
    }
    // Unlike gloves, dormancy and death preserve an existing mask entity.
    if (local->dormant || !engine.render_model(*local) || !local->model_header ||
        !*local->model_header || local->dead)
        return;
    const auto attachment = engine.attachment(*local, "facemask");
    if (attachment == -1)
        return;
    const auto selected = ct ? settings.mask_model_ct.value : settings.mask_model_t.value;
    if (std::uint32_t(selected) >= paths.size())
        return;
    const auto path = paths[std::size_t(selected)];
    const auto model = resolve_model_index(path, models, tables);
    if (model == -1)
        return;
    auto* entity = resolve(engine, state.handle);
    if (entity) {
        if (!entity->networkable || entity->networkable->client_class() != state.factory)
            return;
        if (entity->model_index == model && resolve(engine, entity->parent_handle) == local &&
            entity->model_update_handle == local->handle && entity->owner_handle == local->handle)
            return;
    } else {
        state.handle = invalid_glove_handle;
        entity = create_mask(*state.factory, path.data(), engine, models);
        if (!entity)
            return;
        state.handle = entity->handle;
    }
    engine.begin_model_cache();
    engine.set_model_index(*entity, model);
    engine.end_model_cache();
    zero_transform(*entity, entity->local_origin, 1, engine);
    zero_transform(*entity, entity->local_angles, 2, engine);
    engine.set_parent(*entity, *local, attachment);
    zero_transform(*entity, entity->local_origin, 1, engine);
    zero_transform(*entity, entity->local_angles, 2, engine);
    entity->quality = 0;
    entity->flag_43468bd4 = false;
    engine.entity_boolean_3b8(*entity, false);
    entity->bits_43467044 |= 4;
    entity->flag_43467964 = false;
    entity->flags_43469224 &= 0xFFFBFFFF;
    entity->model_update_handle = local->handle;
    entity->owner_handle = local->handle;
}
} // namespace skeet::skins
