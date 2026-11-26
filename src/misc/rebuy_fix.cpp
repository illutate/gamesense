#include "skeet/misc/rebuy_fix.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/events/engine_event.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) {
    T output;
    std::memcpy(&output, static_cast<const std::byte*>(object) + offset, sizeof output);
    return output;
}
std::uint32_t cvar_integer(void* variable) {
    for (;;) {
        auto* parent = field<void*>(variable, 0x1C);
        if (variable == parent)
            break;
        variable = parent;
    }
    return field<std::uint32_t>(variable, 0x30) ^
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable));
}
constexpr std::uint32_t spawn_hash = 0xC90B4171, purchase_hash = 0xD6CAA6ED;
}

bool RebuyFixRuntime::local_event(events::GameEventReader& event) {
    events::FieldValue user;
    if (event.read(0x8361150E, events::FieldType::integer, user) || user.integer == 0xFFFFFFFF)
        return false;
    const auto index = engine::invoke<std::int32_t>(engine, 9, user.integer);
    return index == engine::invoke<std::int32_t>(engine, 12);
}
void RebuyFixRuntime::player_spawn(events::GameEventReader& event) {
    if (!*enabled || !local_event(event))
        return;
    blocked = false;
}
void RebuyFixRuntime::item_purchase(events::GameEventReader& event) {
    item_purchase(event, local_player);
}
void RebuyFixRuntime::item_purchase(events::GameEventReader& event, void* local) {
    if (!*enabled || !local_event(event))
        return;
    if (!(std::fabs(last_buy_time - *current_time) > 0.25f))
        return;
    inventory.clear();
    for (std::uint32_t offset = 0; offset != 0xC0; offset += 4) {
        const auto handle = field<std::uint32_t>(local, offsets.inventory + offset);
        if (handle == 0xFFFFFFFF)
            continue;
        auto* weapon = engine::invoke<void*>(entity_list, 4, handle);
        if (!weapon)
            continue;
        auto* data = engine::invoke<void*>(weapon, 461);
        const auto identifier = field<std::int32_t>(data, 0xA8);
        if (identifier > 0)
            inventory.push_back(identifier);
    }
    if (field<std::int32_t>(local, offsets.armor) > 0)
        inventory.push_back(field<std::uint8_t>(local, offsets.helmet) ? 33 : 32);
}
void RebuyFixRuntime::update(const UserCommand& command) {
    const auto process = [&] {
        if (!*enabled)
            return;
        const auto anywhere = cvar_integer(buy_anywhere_cvar);
        if (anywhere != 1 && anywhere != field<std::uint32_t>(local_player, offsets.team) &&
            !field<std::uint8_t>(local_player, offsets.buy_zone))
            return;
        if (blocked || !field<std::uint8_t>(local_player, offsets.field_f7df3858 + 4))
            return;
        if (!(command.buttons & 0x2000) || (previous_buttons & 0x2000))
            return;
        last_buy_time = static_cast<float>(*time_4347a500 + *time_4347b118) + *current_time;
        // The native loop snapshots its end pointer. Engine calls do not extend
        // this inventory; item_purchase is time-gated during the command batch.
        const auto count = inventory.size();
        for (std::size_t index = 0; index != count; ++index) {
            const auto identifier = inventory[index];
            if (static_cast<std::uint32_t>(identifier) - 8 < 18) {
                if (engine::invoke<void*>(local_player, 264, 0))
                    continue;
            } else if (identifier == 2)
                continue;
            char text[32];
            std::snprintf(text, sizeof text, "buy %d %d", identifier, identifier);
            engine::invoke<void>(engine, 6, text, true);
        }
        engine::invoke<void>(engine, 114, "buymenu 0", true);
    };
    process();
    previous_buttons = command.buttons;
}
void RebuyFixRuntime::spawn_listener(void* owner, events::GameEventReader& event,
                                     events::EventPlayerIndices&) {
    static_cast<RebuyFixRuntime*>(owner)->player_spawn(event);
}
void RebuyFixRuntime::purchase_listener(void* owner, events::GameEventReader& event,
                                        events::EventPlayerIndices& players) {
    //The production EventMaster supplies EngineEventPlayers around the local
    //entity captured once after its pre-event observers, as in native EDX.
    const auto local = static_cast<events::EngineEventPlayers&>(players).local_entity();
    static_cast<RebuyFixRuntime*>(owner)->item_purchase(event, local);
}
void RebuyFixRuntime::bind_spawn(events::EventRegistry& registry, bool connected) {
    registry.add({spawn_hash, this, spawn_listener}, connected);
}
void RebuyFixRuntime::bind_spawn(events::EventRegistry& registry) {
    registry.add({spawn_hash, this, spawn_listener});
}
void RebuyFixRuntime::sync_purchase_subscription(events::EventRegistry& registry, bool connected) {
    const auto value = *enabled;
    if (value == purchase_subscription)
        return;
    purchase_subscription = value;
    const events::EventListener listener{purchase_hash, this, purchase_listener};
    if (value)
        registry.add(listener, connected);
    else
        registry.remove(listener, connected);
}
void RebuyFixRuntime::sync_purchase_subscription(events::EventRegistry& registry) {
    const auto value = *enabled;
    if (value == purchase_subscription)
        return;
    purchase_subscription = value;
    const events::EventListener listener{purchase_hash, this, purchase_listener};
    if (value)
        registry.add(listener);
    else
        registry.remove(listener);
}
void RebuyFixRuntime::disable_purchase_subscription(events::EventRegistry& registry) {
    if (!purchase_subscription)
        return;
    purchase_subscription = 0;
    registry.remove({purchase_hash, this, purchase_listener});
}
void RebuyFixRuntime::unbind_events(events::EventRegistry& registry, bool connected) {
    registry.remove({spawn_hash, this, spawn_listener}, connected);
    registry.remove({purchase_hash, this, purchase_listener}, connected);
    purchase_subscription = 0;
}
} // namespace skeet::misc
