#include "skeet/skins/inventory.hpp"
#include <algorithm>

namespace skeet::skins {
bool inventory_owner_matches(const WeaponOwnership& owner, std::uint32_t tag, bool valid,
                             PlayerIdentity local, std::int32_t index) noexcept {
    if (valid && (local.low || local.high) && owner.original_owner.low == local.low &&
        owner.original_owner.high == local.high)
        return true;
    if (owner.original_owner.low || owner.zero_marker || tag)
        return false;
    const auto owner_index = owner.owner_handle == 0xFFFFFFFF
                                 ? 0x1FFFU
                                 : std::uint32_t(std::uint16_t(owner.owner_handle));
    return owner_index == std::uint32_t(index);
}
void apply_inventory(const WeaponSkinsConfig& settings, std::span<const InventoryEntry> entries,
                     InventoryEngine& engine) {
    if (!engine.local_player_exists())
        return;
    if (std::none_of(settings.value.begin(), settings.value.end(),
                     [](const auto& r) { return r.enabled != 0; }))
        return;
    if (engine.local_player_dead())
        return;
    for (const auto& entry : entries) {
        auto* item = engine.resolve(entry.handle);
        if (!item || !item->is_weapon)
            continue;
        PlayerIdentity identity;
        const bool valid = engine.local_identity(identity);
        if (!inventory_owner_matches(item->ownership, entry.tag, valid, identity,
                                     engine.local_player_index()))
            continue;
        const auto slot = weapon_config_slot(item->definition, engine.local_team());
        if (slot < 0 || !settings.value[slot].enabled)
            continue;
        PlayerIdentity account;
        engine.local_identity(account);
        apply_attributes(settings.value[slot], item->attributes, account.low, item->entity_class,
                         engine.counter_classes(), engine, engine);
    }
}
} // namespace skeet::skins
