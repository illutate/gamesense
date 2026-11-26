#include "skeet/events/deaths.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::events {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void assign(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void* active_weapon(const DeathBindings& bindings, const WeaponDeathOffsets& offsets) {
    const auto handle = field<std::uint32_t>(bindings.local_player, offsets.active_weapon);
    if (handle == 0xFFFFFFFF)
        return nullptr;
    return engine::invoke<void*>(bindings.entity_list, 4, handle);
}
}
void death_clear_dormant_alpha(GameEventReader& event, const DeathBindings& bindings,
                               esp::PlayerRuntimeState& state) {
    FieldValue victim;
    if (event.read(0x8361150E, FieldType::integer, victim))
        return;
    const auto index = engine::invoke<std::int32_t>(bindings.engine, 9, victim.integer);
    if (static_cast<std::uint32_t>(index) - 1 > 63)
        return;
    const auto entity = engine::invoke<void*>(bindings.entity_list, 3, index);
    if (!entity || !engine::invoke<bool>(entity, 158))
        return;
    if (!engine::invoke<bool>(static_cast<std::byte*>(entity) + 8, 9))
        return;
    state.geometry(static_cast<std::uint32_t>(index) & 63).alpha = 0;
}
DeathWeaponIcons::DeathWeaponIcons(const skins::Settings& settings,
                                   const skins::SequenceRuntime& sequence) noexcept
    : settings(settings), sequence(sequence), selected_models(skins::knife_models()) {}
void DeathWeaponIcons::player_death(MutableGameEvent& event, const DeathBindings& bindings,
                                    const WeaponDeathOffsets& offsets) {
    if (!bindings.local_player)
        return;
    if (engine::invoke<bool>(bindings.engine, 82))
        return;
    if (engine::invoke<bool>(bindings.engine, 93))
        return;
    const auto team = field<std::int32_t>(bindings.local_player, offsets.team);
    if (!(team == 3 ? settings.knife_ct.value : settings.knife_t.value))
        return;
    // 4341A0E8 captures this live producer value once, before reading fields.
    const auto original_definition = sequence.original_definition;
    if (original_definition == -1)
        return;
    FieldValue attacker, weapon;
    if (event.read(0x455B4DBF, FieldType::integer, attacker))
        return;
    if (event.read(0xFDF97DB0, FieldType::string, weapon))
        return;
    const auto source = engine::invoke<std::int32_t>(bindings.engine, 9, attacker.integer);
    if (source !=
        engine::invoke<std::int32_t>(static_cast<std::byte*>(bindings.local_player) + 8, 10))
        return;
    const auto item = active_weapon(bindings, offsets);
    if (!item)
        return;
    const auto selection = field<std::int32_t>(bindings.local_player, offsets.team) == 3
                               ? settings.knife_model_ct.value
                               : settings.knife_model_t.value;
    const auto definition = field<std::uint16_t>(item, offsets.definition);
    if (selected_models[selection].definition != definition || !weapon.string || !*weapon.string)
        return;
    const auto expected = event_hash(weapon.string);
    assign(item, offsets.definition, static_cast<std::uint16_t>(original_definition));
    const auto previous_name = engine::invoke<const char*>(item, 459);
    assign(item, offsets.definition, definition);
    if (expected != event_hash(previous_name + 7))
        return;
    const auto replacement = engine::invoke<const char*>(item, 459);
    if (replacement)
        event.set_string(weapon_key, replacement + 7);
}
void StatTrakDeath::player_death(GameEventReader& event, const DeathBindings& bindings,
                                 const WeaponDeathOffsets& offsets) {
    FieldValue attacker;
    if (event.read(0x455B4DBF, FieldType::integer, attacker))
        return;
    const auto source = engine::invoke<std::int32_t>(bindings.engine, 9, attacker.integer);
    if (source !=
        engine::invoke<std::int32_t>(static_cast<std::byte*>(bindings.local_player) + 8, 10))
        return;
    const auto item = active_weapon(bindings, offsets);
    if (!item)
        return;
    const auto owner_high = field<std::uint32_t>(item, offsets.original_owner_high);
    const auto owner_low = field<std::uint32_t>(item, offsets.original_owner_low);
    std::array<std::byte, 0x158> info{};
    const auto index = engine::invoke<std::int32_t>(bindings.engine, 12);
    if (!engine::invoke<bool>(bindings.engine, 8, index, info.data()))
        return;
    const auto id_low = field<std::uint32_t>(info.data(), 8),
               id_high = field<std::uint32_t>(info.data(), 12);
    if ((!id_low && !id_high) || owner_low != id_low || owner_high != id_high)
        return;
    auto definition = static_cast<std::uint32_t>(field<std::uint16_t>(item, offsets.definition));
    if (!definition)
        return;
    if (definition - 500 <= 25) {
        definition = 42;
        const auto team = field<std::int32_t>(bindings.local_player, offsets.team);
        if (team != 3 && field<std::int32_t>(bindings.local_player, offsets.team) == 2)
            definition = 59;
    } else if (definition >= 65)
        return;
    const auto& settings = weapons.value[definition];
    if (!settings.enabled || !settings.stattrak)
        return;
    assign(item, offsets.stattrak, field<std::uint32_t>(item, offsets.stattrak) + 1);
    const auto count = field<std::int32_t>(item, offsets.attributes + 0x10);
    if (count <= 0)
        return;
    auto attribute = field<std::byte*>(item, offsets.attributes + 4);
    for (std::int32_t entry = 0; entry < count; ++entry, attribute += 0x18)
        if (field<std::uint32_t>(attribute, 4) == 80) {
            assign(attribute, 8, field<std::uint32_t>(attribute, 8) + 1);
            return;
        }
}
} // namespace skeet::events
