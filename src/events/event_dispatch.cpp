#include "skeet/events/event_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/misc/persistent_kill_feed.hpp"
#include "skeet/script/client_api.hpp"
#include <algorithm>
#include <cstring>

namespace skeet::events {
void register_event_observer(engine::OwnerRegistry& registry, EventObserver& observer,
                             engine::JobMemory memory) {
    void* pointer = &observer;
    engine::append_owner(registry, &pointer, memory);
}
bool EventMaster::process(void* receiver, void* packet) {
    if (engine::invoke<bool>(bindings_.engine, 82) || engine::invoke<bool>(bindings_.engine, 93)) {
        const auto table = *static_cast<void***>(receiver);
        const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
        // The native encoded byte displacement resolves to0x88: virtual34.
        const auto original =
            reinterpret_cast<engine::MemberFunction<bool, void*>>(metadata->original_table[34]);
        return original(receiver, packet);
    }
    void* raw_event = engine::invoke<void*, void*>(bindings_.event_manager, 13, packet);
    if (!raw_event)
        return true;
    EngineGameEventReader event(raw_event);
    std::uint32_t passthrough;
    std::memcpy(&passthrough, static_cast<const std::byte*>(packet) + 0x20, sizeof passthrough);
    if (passthrough == 1)
        event.set_boolean("realtime_passthrough", true);
    const auto hash = event_hash(event.name());

    // Native captures both pointer bounds before calling any pre-event method.
    auto** current = observers_.begin;
    auto** const end = observers_.end;
    for (; current != end; ++current)
        static_cast<EventObserver*>(*current)->before(event, hash);

    void* const local = *bindings_.local_player;
    if (local) {
        EngineEventPlayers players(bindings_.engine, local);
        registry_.dispatch(hash, event, &players);
    }
    engine::invoke<void, void*>(bindings_.event_manager, 9, raw_event);

    // The manager call can change the observer vector. Native reloads both
    // bounds and traverses the resulting sequence in reverse order.
    auto** const begin = observers_.begin;
    current = observers_.end;
    while (current != begin) {
        --current;
        static_cast<EventObserver*>(*current)->after(event, hash);
    }
    return true;
}
void KillFeedEventObserver::before(EngineGameEventReader& event, std::uint32_t hash) {
    runtime_.on_event(event, hash);
}
void KillFeedEventObserver::after(EngineGameEventReader&, std::uint32_t) {
    runtime_.reset();
}
void LuaEventObserver::before(EngineGameEventReader& event, std::uint32_t hash) {
    if (!*local_)
        return;
    const auto subscriptions = module_.event_subscriptions();
    if (std::find(subscriptions.begin(), subscriptions.end(), hash) != subscriptions.end())
        module_.dispatch_engine_event(event.raw_event());
}
} // namespace skeet::events
