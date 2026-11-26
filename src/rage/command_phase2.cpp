#include "skeet/rage/command_phase2.hpp"
#include "skeet/rage/preamble.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool accuracy_mode(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
bool rule_blocks_fire(const CommandPhase2Bindings& bindings) noexcept {
    return bindings.game_rules && *bindings.game_rules &&
           field<std::uint8_t>(*bindings.game_rules, bindings.offsets.fire_state.rule_d720aea6) !=
               0;
}
void prepare_revolver(PreambleState& state, void* player, void* weapon, client::Command& command,
                      const CommandPhase2Bindings& bindings) {
    auto buttons = command.movement.buttons;
    if (buttons & 0x2000u)
        return;
    const auto& offsets = bindings.offsets.fire_state;
    if (field<std::uint8_t>(player, offsets.player_block_bcb263a5))
        return;
    if (accuracy_mode(bindings.accuracy_convar) || rule_blocks_fire(bindings))
        return;
    if (field<std::uint8_t>(player, offsets.player_state_a15c3105))
        return;
    if (field<std::int32_t>(weapon, offsets.ammunition) == 0)
        return;
    const auto next_tick = std::bit_cast<std::int32_t>(
        std::uint32_t(field<std::int32_t>(player, bindings.offsets.tick_base)) + 1u);
    const float time = float(next_tick) * bindings.interval;
    // COMISS/JB rejects unordered player timing. The later JBE branches
    // also distinguish equality and unordered values from ordered greater.
    if (!(time >= field<float>(player, offsets.player_next_attack)))
        return;
    if (time >= field<float>(weapon, offsets.next_primary) &&
        field<std::int32_t>(weapon, offsets.weapon_state_9e2078a1) == 208 &&
        time > field<float>(weapon, offsets.postpone_fire)) {
        if (!(field<float>(weapon, offsets.next_secondary) > time))
            return;
        buttons |= 0x800u;
    } else
        buttons |= 1u;
    command.movement.buttons = buttons;
    state.weapon_flags |= 1u;
}
}
void process_rage_command_phase2(PreambleState& state, void* player, void* weapon,
                                 client::Command& command, std::array<float, 3>& incoming_angles,
                                 std::uint8_t& send_packet, const CommandPhase2Bindings& bindings) {
    (void)incoming_angles;
    (void)send_packet;
    state.weapon_flags = 0;
    if (!bindings.settings.enabled.value ||
        !hotkey_active(bindings.settings.activation.value.binding, bindings.input) || !weapon)
        return;
    const auto& offsets = bindings.offsets.fire_state;
    if (!bindings.field_4347c9ec && field<std::uint16_t>(weapon, offsets.weapon_definition) == 64)
        prepare_revolver(state, player, weapon, command, bindings);
    if (field<std::int32_t>(weapon, offsets.ammunition) == 0 &&
        field<std::int32_t>(weapon, bindings.offsets.reserve_ammunition) > 0)
        command.movement.buttons = (command.movement.buttons & ~1u) | 0x2000u;
}
} //namespace skeet::rage
