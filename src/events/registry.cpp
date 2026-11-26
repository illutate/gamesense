#include "skeet/events/registry.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <cstring>

namespace skeet::events {
namespace {
bool equal(const EventListener& a, const EventListener& b) {
    return a.name_hash == b.name_hash && a.owner == b.owner && a.callback == b.callback;
}
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
void fire(void* owner, GameEventReader& event, EventPlayerIndices& players) {
    weapon_fire(*static_cast<GameEventState*>(owner), event, players);
}
void hurt(void* owner, GameEventReader& event, EventPlayerIndices& players) {
    player_hurt(*static_cast<GameEventState*>(owner), event, players);
}
void death(void* owner, GameEventReader& event, EventPlayerIndices& players) {
    player_death(*static_cast<GameEventState*>(owner), event, players);
}
}
bool EventRegistry::add(EventListener listener, bool connected) {
    if (std::find_if(listeners.begin(), listeners.end(),
                     [&](const auto& item) { return equal(item, listener); }) != listeners.end())
        return false;
    listeners.push_back(listener);
    if (connected)
        subscriptions_dirty = true;
    return true;
}
bool EventRegistry::remove(EventListener listener, bool connected) {
    auto found = std::find_if(listeners.begin(), listeners.end(),
                              [&](const auto& item) { return equal(item, listener); });
    if (found == listeners.end())
        return false;
    listeners.erase(found);
    if (connected)
        subscriptions_dirty = true;
    return true;
}
bool EventRegistry::add(EventListener listener) {
    if (!add(listener, false))
        return false;
    if (engine::invoke<bool>(engine_interface, 26)) {
        subscriptions_dirty = true;
        *(static_cast<std::uint8_t*>(event_manager) + 0xA0) = 1;
    }
    return true;
}
bool EventRegistry::remove(EventListener listener) {
    if (!remove(listener, false))
        return false;
    if (engine::invoke<bool>(engine_interface, 26)) {
        subscriptions_dirty = true;
        *(static_cast<std::uint8_t*>(event_manager) + 0xA0) = 1;
    }
    return true;
}
void EventRegistry::dispatch(std::uint32_t hash, GameEventReader& event,
                             EventPlayerIndices* player) {
    if (!player)
        return;
    // The native dispatcher captures its end pointer before invoking callbacks.
    const auto count = listeners.size();
    for (std::size_t index = 0; index != count; ++index) {
        auto listener = listeners[index];
        if (listener.name_hash == hash)
            listener.callback(listener.owner, event, *player);
    }
}
bool EventRegistry::contains(std::uint32_t hash) const noexcept {
    for (const auto& listener : listeners)
        if (listener.name_hash == hash)
            return true;
    return false;
}
void bind_shot_events(EventRegistry& registry, GameEventState& state, bool connected) {
    registry.add({weapon_fire_hash, &state, fire}, connected);
    registry.add({player_hurt_hash, &state, hurt}, connected);
    registry.add({player_death_hash, &state, death}, connected);
}
void unbind_shot_events(EventRegistry& registry, GameEventState& state, bool connected) {
    registry.remove({weapon_fire_hash, &state, fire}, connected);
    registry.remove({player_hurt_hash, &state, hurt}, connected);
    registry.remove({player_death_hash, &state, death}, connected);
}
void bind_shot_events(EventRegistry& registry, GameEventState& state) {
    registry.add({weapon_fire_hash, &state, fire});
    registry.add({player_hurt_hash, &state, hurt});
    registry.add({player_death_hash, &state, death});
}
void unbind_shot_events(EventRegistry& registry, GameEventState& state) {
    registry.remove({weapon_fire_hash, &state, fire});
    registry.remove({player_hurt_hash, &state, hurt});
    registry.remove({player_death_hash, &state, death});
}
void dispatch_shot_event(GameEventState& state, std::uint32_t hash, GameEventReader& event,
                         EventPlayerIndices& player) {
    switch (hash) {
    case weapon_fire_hash:
        weapon_fire(state, event, player);
        break;
    case bullet_impact_hash:
        bullet_impact(state, event, player);
        break;
    case player_hurt_hash:
        player_hurt(state, event, player);
        break;
    case player_death_hash:
        player_death(state, event, player);
        break;
    }
}
void enable_event_subscriptions(const EventRegistry& registry,
                                std::span<const std::uint32_t> lua_names,
                                std::span<const SubscriptionDescriptor> descriptors,
                                std::span<std::uint32_t> mask) {
    if (registry.listeners.empty() && lua_names.empty())
        return;
    for (const auto& item : descriptors) {
        if (!item.name)
            continue;
        const auto hash = event_hash(item.name);
        if (!registry.contains(hash) &&
            std::find(lua_names.begin(), lua_names.end(), hash) == lua_names.end())
            continue;
        const auto identifier = static_cast<std::uint32_t>(item.identifier);
        const auto bit = std::uint32_t{1} << (identifier % 32);
        if (!(mask[identifier / 32] & bit))
            mask[identifier / 32] |= bit;
    }
}
void enable_engine_event_subscriptions(const EventRegistry& registry,
                                       std::span<const std::uint32_t> lua_names,
                                       const void* manager, void* mask) {
    if (registry.listeners.empty() && lua_names.empty())
        return;
    const auto* descriptor = field<const std::byte*>(manager, 4);
    const auto* const end = descriptor + field<std::int32_t>(manager, 0x10) * 0x34;
    for (; descriptor != end; descriptor += 0x34) {
        const auto* const names = field<const std::byte*>(manager, 0x78);
        if (!names)
            continue;
        const auto name_index = field<std::int32_t>(descriptor, 4);
        const auto* const name =
            field<const char*>(names, std::ptrdiff_t(name_index) * 0x18 + 0x10);
        if (!name)
            continue;
        const auto hash = event_hash(name);
        if (!registry.contains(hash) &&
            std::find(lua_names.begin(), lua_names.end(), hash) == lua_names.end())
            continue;
        const auto identifier = field<std::int32_t>(descriptor, 0);
        auto* const words = field<std::uint32_t*>(mask, 0xC);
        const auto bit = std::uint32_t{1} << (static_cast<std::uint32_t>(identifier) & 31);
        auto& word = words[identifier / 32];
        if (!(word & bit))
            word |= bit;
    }
}
} // namespace skeet::events
