#include "skeet/misc/purchase_log.hpp"
#include "skeet/engine/interface_call.hpp"

namespace skeet::misc {
namespace {
constexpr std::uint32_t purchase_hash = 0xD6CAA6ED;
}

void PurchaseLogRuntime::item_purchase(events::GameEventReader& event,
                                       events::EngineEventPlayers& players) {
    if (!settings.purchases.value)
        return;
    events::FieldValue user, weapon;
    if (event.read(0x8361150E, events::FieldType::integer, user))
        return;
    if (event.read(0xFDF97DB0, events::FieldType::string, weapon))
        return;
    const auto index = players.player_for_user_id(user.integer);
    if (static_cast<std::uint32_t>(index) - 1 > 63)
        return;
    if (index == players.local_player_index())
        return;
    auto* target = engine::invoke<void*>(entity_list, 3, index);
    if (!target)
        return;
    const auto filter = targets.capture(players.local_entity());
    if (!rage::target_is_enemy(filter, target))
        return; // complete4339EF8C
    if (!weapon.string || events::event_hash(weapon.string) == 0x4DC70629)
        return;
    const auto name = events::player_log_name(index, players);
    std::array<char, 16> decoded_format;
    decode_native_text(format, decoded_format.data());
    output.format(decoded_format.data(), name.data(), weapon.string);
}

void PurchaseLogRuntime::purchase_listener(void* owner, events::GameEventReader& event,
                                           events::EventPlayerIndices& players) {
    static_cast<PurchaseLogRuntime*>(owner)->item_purchase(
        event, static_cast<events::EngineEventPlayers&>(players));
}
void PurchaseLogRuntime::damage_listener(void* owner, events::GameEventReader& event,
                                         events::EventPlayerIndices& players) {
    auto& runtime = *static_cast<PurchaseLogRuntime*>(owner);
    runtime.damage.player_hurt(event, static_cast<events::HitLogPlayerInfo&>(players),
                               runtime.output);
}
void PurchaseLogRuntime::bind_events(events::EventRegistry& registry) {
    registry.add({events::player_hurt_hash, this, damage_listener});
}
void PurchaseLogRuntime::configuration_changed(bool notify, events::EventRegistry& registry) {
    if (!notify)
        return;
    const auto enabled = settings.purchases.value;
    if (enabled == purchase_subscribed_)
        return;
    purchase_subscribed_ = enabled;
    const events::EventListener listener{purchase_hash, this, purchase_listener};
    if (enabled)
        registry.add(listener);
    else
        registry.remove(listener);
}
void PurchaseLogRuntime::disable_purchase_subscription(events::EventRegistry& registry) {
    if (purchase_subscribed_) {
        purchase_subscribed_ = 0;
        registry.remove({purchase_hash, this, purchase_listener});
    }
}
void PurchaseLogRuntime::unbind_events(events::EventRegistry& registry) {
    disable_purchase_subscription(registry);
    registry.remove({events::player_hurt_hash, this, damage_listener});
}
} // namespace skeet::misc
