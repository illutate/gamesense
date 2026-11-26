#include "skeet/events/feature_listeners.hpp"
#include "skeet/events/engine_event.hpp"

namespace skeet::events {
void HitLogListener::dispatch(void* context, GameEventReader& event, EventPlayerIndices& players) {
    auto& self = *static_cast<HitLogListener*>(context);
    self.owner.player_hurt(event, static_cast<HitLogPlayerInfo&>(players), self.output);
}
void HitLogListener::bind(EventRegistry& registry) {
    registry.add({player_hurt_hash, this, dispatch});
}
void HitLogListener::unbind(EventRegistry& registry) {
    registry.remove({player_hurt_hash, this, dispatch});
}
void HitMarkerListener::dispatch(void* context, GameEventReader& event,
                                 EventPlayerIndices& players) {
    auto& self = *static_cast<HitMarkerListener*>(context);
    self.owner.player_hurt(event, self.bindings,
                           static_cast<EngineEventPlayers&>(players).local_entity());
}
void HitMarkerListener::bind(EventRegistry& registry) {
    registry.add({player_hurt_hash, this, dispatch});
}
void HitMarkerListener::unbind(EventRegistry& registry) {
    registry.remove({player_hurt_hash, this, dispatch});
}
void DeathWeaponIconsListener::dispatch(void* context, GameEventReader& event,
                                        EventPlayerIndices& players) {
    auto& self = *static_cast<DeathWeaponIconsListener*>(context);
    auto bindings = self.bindings;
    bindings.local_player = static_cast<EngineEventPlayers&>(players).local_entity();
    self.owner.player_death(static_cast<MutableGameEvent&>(event), bindings, self.offsets);
}
void DeathWeaponIconsListener::bind(EventRegistry& registry) {
    registry.add({player_death_hash, this, dispatch});
}
void DeathWeaponIconsListener::unbind(EventRegistry& registry) {
    registry.remove({player_death_hash, this, dispatch});
}
void StatTrakDeathListener::dispatch(void* context, GameEventReader& event,
                                     EventPlayerIndices& players) {
    auto& self = *static_cast<StatTrakDeathListener*>(context);
    auto bindings = self.bindings;
    bindings.local_player = static_cast<EngineEventPlayers&>(players).local_entity();
    self.owner.player_death(event, bindings, self.offsets);
}
void StatTrakDeathListener::bind(EventRegistry& registry) {
    registry.add({player_death_hash, this, dispatch});
}
void StatTrakDeathListener::unbind(EventRegistry& registry) {
    registry.remove({player_death_hash, this, dispatch});
}
void DormantAlphaDeathListener::dispatch(void* context, GameEventReader& event,
                                         EventPlayerIndices&) {
    auto& self = *static_cast<DormantAlphaDeathListener*>(context);
    death_clear_dormant_alpha(event, self.bindings, self.players);
}
void DormantAlphaDeathListener::bind(EventRegistry& registry) {
    registry.add({player_death_hash, this, dispatch});
}
void DormantAlphaDeathListener::unbind(EventRegistry& registry) {
    registry.remove({player_death_hash, this, dispatch});
}
} // namespace skeet::events
