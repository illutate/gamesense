#include "skeet/esp/world_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include "world_engine_detail.hpp"

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace world_detail;
void* WorldEntityEngine::by_index(std::int32_t index) const {
    return bindings_.queries.grenades().by_index(index);
}
bool WorldEntityEngine::dormant(void* entity) const {
    return engine::invoke<bool>(static_cast<std::byte*>(entity) + 8, 9);
}
std::uint32_t WorldEntityEngine::client_class_index(void* entity) const {
    const auto* type = engine::invoke<const void*>(static_cast<std::byte*>(entity) + 8, 2);
    return field<std::uint32_t>(type, 0x14);
}
bool WorldEntityEngine::is_weapon(void* entity) const {
    return engine::invoke<bool>(entity, 166);
}
std::int32_t WorldEntityEngine::highest_entity_index() const {
    return field<std::int32_t>(bindings_.queries.grenades().bindings().entity_list, 0x24);
}
std::int32_t WorldEntityEngine::game_type() const {
    return convar_integer(bindings_.game_type);
}
std::int32_t WorldEntityEngine::ammunition(const WorldEntity& entity) const {
    return field<std::int32_t>(reinterpret_cast<const void*>(entity.identity),
                               bindings_.offsets.ammunition);
}
std::int32_t WorldEntityEngine::ammunition_capacity(const WorldEntity& entity) const {
    return engine::invoke<std::int32_t>(reinterpret_cast<void*>(entity.identity), 375);
}
char WorldEntityEngine::bomb_site(std::int32_t index) const {
    if (std::uint32_t(index) & 0xFFFF0000u)
        return 'A';
    auto* entity = by_index(index);
    if (!entity)
        return 'A';
    auto* slot = bindings_.queries.grenades().bindings().player_resource_slot;
    if (!slot)
        return 'A';
    auto* resource = field<void*>(slot, 0);
    if (!resource)
        return 'A';
    // The two SDK calls precede every coordinate load, including the first
    // returned bounds vector. Their providers can mutate the same storage.
    const auto* maximum = engine::invoke<const Vector3*>(entity, 77);
    const auto* minimum = engine::invoke<const Vector3*>(entity, 76);
    const Vector3 center{(maximum->x - minimum->x) * 0.5f + minimum->x,
                         (maximum->y - minimum->y) * 0.5f + minimum->y,
                         (maximum->z - minimum->z) * 0.5f + minimum->z};
    const auto first = field<Vector3>(resource, bindings_.offsets.bomb_site_first);
    const auto second = field<Vector3>(resource, bindings_.offsets.bomb_site_second);
    return squared(center, first) > squared(center, second) ? 'B' : 'A';
}
void WorldEntityEngine::dispatch(ui::RenderQueue& queue, EspFrameOwner& owner, std::int32_t& cursor,
                                 const EspFrameContext& frame, RuntimePlayer& view, void* entity,
                                 EntityOverlayHandler handler) {
    switch (handler) {
    case EntityOverlayHandler::hostage:
        hostage(queue, frame, view, entity);
        break;
    case EntityOverlayHandler::dropped_bomb:
        dropped_bomb(queue, owner, frame, entity);
        break;
    case EntityOverlayHandler::planted_bomb:
        planted_bomb(queue, owner, cursor, frame, view, entity);
        break;
    case EntityOverlayHandler::grenade_projectile:
        grenade(queue, owner, frame, entity);
        break;
    case EntityOverlayHandler::inferno:
        inferno(queue, owner, frame, view, entity);
        break;
    case EntityOverlayHandler::danger_zone_item:
        danger_zone(owner, view, entity);
        break;
    case EntityOverlayHandler::defuse_kit:
        defuse_kit(queue, owner, frame, view, entity);
        break;
    case EntityOverlayHandler::none:
        break;
    }
}
void WorldEntityEngine::collect_dropped(WorldEntityScan& scan,
                                        std::array<WorldEntity, 65>& frame_entities, void* entity,
                                        std::int32_t index, std::uint32_t options,
                                        const EspFrameOwner& owner, const EspFrameContext& frame) {
    if (!options)
        return;
    const auto& offsets = bindings_.offsets;
    std::uint16_t definition;
    if (is_weapon(entity)) {
        if (field<std::int32_t>(entity, offsets.owner_handle) != -1)
            return;
        definition = field<std::uint16_t>(entity, offsets.definition);
        if (ammunition_weapon(definition)) {
            const auto clip = field<std::int32_t>(entity, offsets.ammunition);
            if (clip < 0 ||
                detail::add(field<std::int32_t>(entity, offsets.reserve_ammunition), clip) <= 0)
                return;
        }
    } else {
        if (owner.defuse_kit_model == -1 ||
            field<std::int16_t>(entity, offsets.model_index) != owner.defuse_kit_model ||
            field<std::int32_t>(entity, offsets.owner_handle) != -1)
            return;
        definition = 55;
    }
    Point screen;
    if (!project_point(frame.draw.projection, field<Vector3>(entity, offsets.origin), screen) ||
        definition >= 65)
        return;
    const auto distance = squared(bindings_.view_origin, field<Vector3>(entity, offsets.origin));
    auto& selected = scan.dropped[definition];
    if (selected.entity && !(distance < selected.distance_squared))
        return;
    auto& value = frame_entities[definition];
    value.identity = reinterpret_cast<std::uintptr_t>(entity);
    value.index = static_cast<std::uint32_t>(index);
    value.definition = definition;
    selected = {&value, distance, screen};
}
void WorldEntityEngine::danger_zone(EspFrameOwner& owner, RuntimePlayer& view, void* entity) {
    const auto model = field<std::int16_t>(entity, bindings_.offsets.model_index);
    if (model < 0)
        return;
    const auto* item = owner.entity_models.models.find(static_cast<std::uint16_t>(model));
    if (!item || !item->setting->enabled.value)
        return;
    WorldEntity value;
    value.model_index = model;
    value.origin = field<Vector3>(entity, bindings_.offsets.origin);
    collect_danger_zone_model(owner.entity_models, value,
                              field<Vector3>(view.engine_entity, bindings_.offsets.origin));
}
void WorldEntityEngine::dropped_bomb(ui::RenderQueue& queue, EspFrameOwner& owner,
                                     const EspFrameContext& frame, void* entity) {
    if (!frame.world_config.dropped_weapons.value)
        return;
    if (bindings_.game_rules && *bindings_.game_rules &&
        !field<std::uint8_t>(*bindings_.game_rules, bindings_.offsets.rules_allow_bomb))
        return;
    if (field<std::int32_t>(entity, bindings_.offsets.dropped_bomb_state))
        return;
    Point screen;
    if (!project_point(frame.draw.projection, field<Vector3>(entity, bindings_.offsets.origin),
                       screen))
        return;
    queue.text(screen.x, screen.y, frame.world_config.bomb.color.value, 0x14, 0,
               owner.dropped_bomb_label);
    if (frame.world_config.dropped_weapons.value & 16)
        distance(queue, {screen.x, detail::add(screen.y, 10)},
                 squared(bindings_.view_origin, field<Vector3>(entity, bindings_.offsets.origin)),
                 frame.world_config.bomb.color.value);
}
void WorldEntityEngine::defuse_kit(ui::RenderQueue& queue, const EspFrameOwner& owner,
                                   const EspFrameContext& frame, RuntimePlayer& view,
                                   void* entity) {
    if (!frame.world_config.dropped_weapons.value ||
        field<std::int32_t>(view.engine_entity, bindings_.offsets.team) == 2)
        return;
    if (field<std::int16_t>(entity, bindings_.offsets.model_index) != owner.defuse_kit_model)
        return;
    Point screen;
    if (!project_point(frame.draw.projection, field<Vector3>(entity, bindings_.offsets.origin),
                       screen))
        return;
    queue.text(screen.x, screen.y, frame.world_config.dropped_weapon_color.value, 0x14, 0, u"KIT");
    if (frame.world_config.dropped_weapons.value & 16)
        distance(queue, {screen.x, detail::add(screen.y, 10)},
                 squared(bindings_.view_origin, field<Vector3>(entity, bindings_.offsets.origin)),
                 frame.world_config.dropped_weapon_color.value);
}
} // namespace skeet::esp
