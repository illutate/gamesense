#include "skeet/skins/stattrak.hpp"

namespace skeet::skins {
void player_death(const WeaponSkinsConfig& settings, events::GameEventReader& event,
                  StatTrakEngine& engine) {
    events::FieldValue attacker;
    if (event.read(0x455B4DBF, events::FieldType::integer, attacker))
        return;
    if (engine.player_for_user_id(attacker.integer) != engine.entity_index())
        return;
    auto* weapon = engine.active_weapon();
    if (!weapon)
        return;
    std::uint32_t low = 0, high = 0;
    if (!engine.local_identity(low, high) || (!low && !high))
        return;
    if (weapon->owner_low != low || weapon->owner_high != high)
        return;
    const auto slot = weapon_config_slot(weapon->definition, engine.team());
    if (slot < 0)
        return;
    const auto& record = settings.value[slot];
    if (!record.enabled || !record.stattrak)
        return;
    ++weapon->counter;
    for (auto& attribute : weapon->attributes)
        if (attribute.id == 80) {
            ++attribute.value;
            break;
        }
}
} // namespace skeet::skins
