#include "skeet/esp/frame_events.hpp"
#include <cstring>

namespace skeet::esp {
namespace {
constexpr std::uint32_t pickup_hash = 0x1A7A32B9, spawn_hash = 0xC90B4171;
constexpr std::uint32_t round_hash = 0x3D07D291, planted_hash = 0xDCBA11CF;
constexpr std::uint32_t equip_hash = 0xAF956ECC, begin_hash = 0xEF1E4D35, abort_hash = 0xEDD7EBF0;
bool integer(events::GameEventReader& event, std::uint32_t hash, events::FieldValue& value) {
    return !event.read(hash, events::FieldType::integer, value);
}
}
RuntimePlayer* EspFrameEvents::event_player(std::uint32_t user, events::EventPlayerIndices& indices,
                                            std::int32_t& index) {
    index = indices.player_for_user_id(user);
    if (index < 1 || index > 64)
        return nullptr;
    auto* player = players_.by_index(players_.context, index);
    return player && players_.is_player(players_.context, *player) ? player : nullptr;
}
void EspFrameEvents::item_pickup_slerp(events::GameEventReader& event,
                                       events::EventPlayerIndices& indices) {
    if (!config_.dormant.value)
        return;
    events::FieldValue behavior, user, item;
    if (!integer(event, 0x5D23E3D6, behavior) || !integer(event, 0x8361150E, user) ||
        !integer(event, 0x9BC0F45A, item) || behavior.integer != 1)
        return;
    std::int32_t index;
    if (!event_player(user.integer, indices, index))
        return;
    auto& queue = owner_.pickups;
    const auto next = queue.count + 1;
    queue.count = next < 128 ? next : 128;
    if (queue.count != 1)
        std::memmove(queue.entries.data() + 1, queue.entries.data(),
                     (queue.count - 1) * sizeof(PickupObservation));
    queue.entries[0].item_index = std::uint16_t(item.integer);
    queue.entries[0].player_index = std::uint16_t(index);
    queue.entries[0].frame = *frame_;
}
void EspFrameEvents::player_spawn(events::GameEventReader& event,
                                  events::EventPlayerIndices& indices) {
    auto& queue = owner_.pickups;
    const auto* counter = frame_;
    while (queue.count && queue.entries[queue.count - 1].frame < *counter)
        --queue.count;
    events::FieldValue user;
    if (!integer(event, 0x8361150E, user))
        return;
    std::int32_t index;
    if (!event_player(user.integer, indices, index))
        return;
    auto& geometry = owner_.players.cache.get(std::uint32_t(index))->geometry;
    geometry.last_live_tick = 0;
    geometry.alpha = 0;
}
void EspFrameEvents::item_equip(events::GameEventReader& event,
                                events::EventPlayerIndices& indices) {
    if (!config_.dormant.value)
        return;
    events::FieldValue user, definition;
    if (!integer(event, 0x8361150E, user) || !integer(event, 0x3D4DBA99, definition))
        return;
    std::int32_t index;
    if (!event_player(user.integer, indices, index))
        return;
    owner_.players.cache.get(std::uint32_t(index))->cached_definition =
        std::uint16_t(definition.integer);
}
void EspFrameEvents::site(events::GameEventReader& event) {
    events::FieldValue value;
    if (integer(event, 0xA55BB3BF, value))
        owner_.notice_entity = std::int32_t(value.integer);
}
void EspFrameEvents::bomb_begin_plant(events::GameEventReader& event) {
    owner_.frame.timer = 3.0f;
    site(event);
}
void EspFrameEvents::reset_plant(events::GameEventReader& event) {
    owner_.frame.timer = 0;
    site(event);
}
void EspFrameEvents::round_start(events::GameEventReader& event) {
    owner_.pickups.count = 0;
    reset_plant(event);
}
void EspFrameEvents::pickup(void* self, events::GameEventReader& event,
                            events::EventPlayerIndices& indices) {
    static_cast<EspFrameEvents*>(self)->item_pickup_slerp(event, indices);
}
void EspFrameEvents::spawn(void* self, events::GameEventReader& event,
                           events::EventPlayerIndices& indices) {
    static_cast<EspFrameEvents*>(self)->player_spawn(event, indices);
}
void EspFrameEvents::equip(void* self, events::GameEventReader& event,
                           events::EventPlayerIndices& indices) {
    static_cast<EspFrameEvents*>(self)->item_equip(event, indices);
}
void EspFrameEvents::begin(void* self, events::GameEventReader& event,
                           events::EventPlayerIndices&) {
    static_cast<EspFrameEvents*>(self)->bomb_begin_plant(event);
}
void EspFrameEvents::reset(void* self, events::GameEventReader& event,
                           events::EventPlayerIndices&) {
    static_cast<EspFrameEvents*>(self)->reset_plant(event);
}
void EspFrameEvents::round(void* self, events::GameEventReader& event,
                           events::EventPlayerIndices&) {
    static_cast<EspFrameEvents*>(self)->round_start(event);
}
void EspFrameEvents::bind_before_death(events::EventRegistry& registry) {
    registry.add({pickup_hash, this, pickup});
    registry.add({spawn_hash, this, spawn});
}
void EspFrameEvents::bind_after_death(events::EventRegistry& registry) {
    registry.add({round_hash, this, round});
    registry.add({planted_hash, this, reset});
}
std::array<events::EventListener, 3> EspFrameEvents::dormant_listeners() noexcept {
    return {{{equip_hash, this, equip}, {begin_hash, this, begin}, {abort_hash, this, reset}}};
}
} // namespace skeet::esp
