#include "skeet/misc/quick_switch.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t previous_number(std::int32_t number) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(number) - 1u);
}
std::uint8_t history_redraw(const prediction::CommandHistoryState& history) noexcept {
    // This byte is part of the shared history payload, immediately before
    // its +6D grenade pin state and +70 grenade throw timestamp.
    return field<std::uint8_t>(&history, 0x6C);
}
}

void* QuickSwitchRuntime::active_weapon(void* player) const {
    const auto handle = field<std::uint32_t>(player, bindings.offsets.active_weapon);
    if (handle == UINT32_MAX)
        return nullptr;
    return engine::invoke<void*>(bindings.entity_list, 4, handle);
}

void QuickSwitchRuntime::capture_before_prediction(void* player, const client::Command& command) {
    if (!*enabled || command.weapon_select != 0)
        return;
    auto* weapon = active_weapon(player);
    if (!weapon || engine::invoke<std::int32_t>(weapon, 455) != 9)
        return;
    state.captured_command = command.number;
    state.captured_pin_pulled = field<std::uint8_t>(weapon, bindings.offsets.grenade_pin_pulled);
    state.captured_throw_time = field<float>(weapon, bindings.offsets.grenade_throw_time);
}

void QuickSwitchRuntime::finish_prediction(void* player, const client::Command& command) {
    if (command.number != state.captured_command)
        return;
    auto* weapon = active_weapon(player);
    if (!weapon || engine::invoke<std::int32_t>(weapon, 455) != 9)
        return;
    if (state.captured_throw_time != 0.0f && state.captured_pin_pulled &&
        field<float>(weapon, bindings.offsets.grenade_throw_time) == 0.0f &&
        field<std::uint8_t>(weapon, bindings.offsets.grenade_pin_pulled) == 0 &&
        state.last_switch_command < command.number)
        state.pending_command = command.number;
}

void QuickSwitchRuntime::process_phase2(void* player, void* weapon, client::Command& command,
                                        std::uint8_t& send_packet) {
    if (!*enabled || !weapon) {
        state.pending_command = std::numeric_limits<std::int32_t>::max();
        return;
    }
    const auto& offsets = bindings.offsets;
    if (engine::invoke<std::int32_t>(weapon, 455) == 9) {
        const auto previous = previous_number(command.number);
        const auto* history =
            prediction::CommandHistoryView::from_owner(bindings.history_owner).find(previous);
        if (history) {
            // Native weapon slot2 returns the existing handle/identity word.
            // It is queried only after the exact-number history lookup passes.
            const auto identity = *engine::invoke<const std::uint32_t*>(weapon, 2);
            if (history->weapon_identity == identity) {
                const bool was_throwing = history_redraw(*history) == 0 && history->fired != 0 &&
                                          history->shot_time != 0.0f;
                const bool finished_throw =
                    field<std::uint8_t>(weapon, offsets.grenade_redraw) != 0 &&
                    field<std::uint8_t>(weapon, offsets.grenade_pin_pulled) == 0 &&
                    field<float>(weapon, offsets.grenade_throw_time) == 0.0f;
                if (was_throwing && finished_throw)
                    state.pending_command = previous_number(command.number);
            }
        }
    } else if (field<std::uint16_t>(weapon, offsets.weapon_definition) == 31 &&
               field<std::int32_t>(player, offsets.shots_fired) == 1 &&
               field<std::int32_t>(weapon, offsets.ammunition) == 0) {
        state.pending_command = previous_number(command.number);
    }
    if (*bindings.last_outgoing_command < state.pending_command)
        return;
    state.last_switch_command = command.number;
    state.pending_command = std::numeric_limits<std::int32_t>::max();
    if (bindings.game_rules && *bindings.game_rules) {
        auto* next = engine::invoke<void*>(*bindings.game_rules, 27, player, 0);
        if (next)
            command.weapon_select =
                engine::invoke<std::int32_t>(static_cast<std::byte*>(next) + 8, 10);
    }
    if (*bindings.choked_commands == 0)
        send_packet = 0;
}
} // namespace skeet::misc
