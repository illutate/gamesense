#include "skeet/weapon/fire_state.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

namespace skeet::weapon {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool rule_blocks_fire(const FireStateBindings& bindings) { // complete 433DF22C
    return bindings.game_rules && *bindings.game_rules &&
           field<std::uint8_t>(*bindings.game_rules, bindings.offsets.rule_d720aea6) != 0;
}
}
FireState evaluate_fire_state(const FireStateBindings& bindings, void* player, void* weapon,
                              const client::Command& command) {
    FireState state;
    if (!weapon)
        return state;
    if (command.weapon_select &&
        command.weapon_select !=
            engine::invoke<std::int32_t>(static_cast<std::byte*>(weapon) + 8, 10))
        return state;
    const auto& offsets = bindings.offsets;
    const auto definition = field<std::uint16_t>(weapon, offsets.weapon_definition);
    if ((definition == 4 || definition == 10) && field<std::uint8_t>(weapon, offsets.burst_mode) &&
        field<std::int32_t>(weapon, offsets.burst_remaining) > 0 &&
        *bindings.current_time >= field<float>(weapon, offsets.next_burst)) {
        state.primary_ready = state.burst_ready = true;
        return state;
    }
    if (field<std::uint8_t>(player, offsets.player_block_bcb263a5) || rule_blocks_fire(bindings))
        return state;
    if (field<std::int32_t>(player, offsets.team) == 3 &&
        field<std::uint8_t>(player, offsets.player_state_a15c3105))
        return state;
    if (field<std::int32_t>(weapon, offsets.ammunition) == 0)
        return state;
    const auto primary_time = *bindings.current_time;
    if (field<float>(player, offsets.player_next_attack) > primary_time)
        return state;
    // COMISS/JBE admits unordered primary timestamps. Negating the ordered
    // greater-than comparison preserves that branch for a NaN input.
    if (!(field<float>(weapon, offsets.next_primary) > primary_time)) {
        bool permitted = true;
        if (field<std::int32_t>(player, offsets.shots_fired) > 0)
            permitted = engine::invoke<bool>(weapon, 430);
        if (permitted) {
            if (field<std::uint16_t>(weapon, offsets.weapon_definition) != 64)
                state.primary_ready = true;
            else if (field<std::int32_t>(weapon, offsets.weapon_state_9e2078a1) == 208 &&
                     !(field<float>(weapon, offsets.postpone_fire) >= primary_time))
                state.primary_ready = true;
        }
    }
    // The native owner reads prediction time again after the virtual query.
    if (field<float>(weapon, offsets.next_secondary) > *bindings.current_time)
        return state;
    if (field<std::uint16_t>(weapon, offsets.weapon_definition) == 64) {
        if (field<std::int32_t>(player, offsets.shots_fired) > 0 ||
            field<std::int32_t>(weapon, offsets.weapon_state_9e2078a1) == 208 ||
            (command.movement.buttons & 1))
            return state;
    }
    state.secondary_ready = true;
    return state;
}
} // namespace skeet::weapon
