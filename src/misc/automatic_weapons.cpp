#include "skeet/misc/automatic_weapons.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include <bit>
#include <cstring>

namespace skeet::misc {
namespace {
template <class Value> Value field(const void* object, std::ptrdiff_t offset) noexcept {
    Value value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

void* frame_weapon(const client::CommandFrame& frame) noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
}

std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}

bool first_bomb_nearby(const AutomaticWeaponsBindings& bindings, void* player) {
    // Complete 4340AEDA. Retain both returned references until after the local
    // origin query: an engine call can change the candidate's origin storage.
    const auto* list = *bindings.planted_bombs;
    if (!list || list->count == 0)
        return false;
    auto* bomb = list->entries[0];
    if (!bomb || !field<std::uint8_t>(bomb, bindings.offsets.bomb_ticking))
        return false;
    const auto* bomb_origin = engine::invoke<const std::array<float, 3>*>(bomb, 10);
    const auto* player_origin = engine::invoke<const std::array<float, 3>*>(player, 10);
    const auto x = (*bomb_origin)[0] - (*player_origin)[0];
    const auto y = (*bomb_origin)[1] - (*player_origin)[1];
    const auto z = (*bomb_origin)[2] - (*player_origin)[2];
    return ((y * y) + (x * x)) + (z * z) < 10000.0f;
}

void process_last_second_defuse(const AutomaticWeaponsConfig& config,
                                const AutomaticWeaponsBindings& bindings,
                                const client::CommandFrame& frame, void* player,
                                client::Command& command, InputBitmap input) {
    if (!hotkey_active(config.last_second_defuse.value.binding, input) || !*bindings.planted_bombs)
        return;
    if (field<std::int32_t>(player, bindings.offsets.team) != 3 || !(frame.flags & 0x200000u) ||
        !first_bomb_nearby(bindings, player))
        return;

    // The native margin is 350ms on top of the actual 5/10 second defuse time.
    const auto duration = field<std::uint8_t>(player, bindings.offsets.has_defuser)
                              ? 5.349999904632568f
                              : 10.350000381469727f;
    // Reload the live list after both engine origin queries, as the owner does.
    const auto* bomb = (*bindings.planted_bombs)->entries[0];
    const auto remaining =
        field<float>(bomb, bindings.offsets.bomb_detonation_time) - *bindings.current_time;
    if (remaining <= duration)
        command.movement.buttons |= 0x20u;
}

void release_attack(const client::CommandFrame& frame, client::Command& command) noexcept {
    command.movement.buttons &= ~1u;
    if (frame.weapon_id_d8 == 64)
        command.movement.buttons &= ~0x800u;
}
}

void process_automatic_weapons(AutomaticWeaponsState& state, const AutomaticWeaponsConfig& config,
                               const AutomaticWeaponsBindings& bindings,
                               const client::CommandFrame& frame, void* player,
                               client::Command& command, InputBitmap input) {
    process_last_second_defuse(config, bindings, frame, player, command, input);
    if (!config.enabled.value)
        return;

    const auto& offsets = bindings.offsets;
    if (field<std::uint8_t>(player, offsets.player_blocks_attack))
        command.movement.buttons &= ~0x801u;

    auto* weapon = frame_weapon(frame);
    if (!weapon || !lagcomp::history_weapon_is_firearm(
                       field<std::uint16_t>(weapon, offsets.weapon_definition)))
        return;
    // Revolver mode is handled regardless of its automatic-fire virtual. Other
    // automatic weapons keep the engine's held-attack behavior.
    if (frame.weapon_id_d8 != 64 && engine::invoke<bool>(weapon, 430))
        return;

    if ((frame.flags & 8u) && field<std::int32_t>(player, offsets.shots_fired) > 0) {
        release_attack(frame, command);
        return;
    }

    const auto configured_delay = config.delay.value;
    weapon = frame_weapon(frame);
    const auto last_shot = field<float>(weapon, offsets.weapon_last_shot);
    if (state.last_shot_time != last_shot) {
        state.last_shot_time = last_shot;
        if (configured_delay) {
            auto minimum = std::bit_cast<std::int32_t>(1u - std::uint32_t(configured_delay));
            if (minimum < -30)
                minimum = -30;
            state.delay_adjustment = bindings.random_integer(minimum, 30);
        }
    }
    if (field<std::int32_t>(player, offsets.shots_fired) <= 0)
        return;

    // Both the active weapon and its shot timestamp are read again after the
    // random import. Capture elapsed time before the fire-cycle query.
    weapon = frame_weapon(frame);
    const auto elapsed = *bindings.current_time - field<float>(weapon, offsets.weapon_last_shot);
    if (configured_delay) {
        const auto delay = add(configured_delay, state.delay_adjustment);
        if (delay > 0) {
            const auto cycle = engine::invoke<float>(weapon, 433, std::int32_t(0));
            const auto extra = float(delay) * 0.0010000000474974513f;
            const auto release_after = float(static_cast<long double>(cycle) + extra);
            if (release_after > elapsed)
                return;
        }
    }
    release_attack(frame, command);
}

} // namespace skeet::misc
