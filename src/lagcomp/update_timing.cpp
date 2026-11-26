#include "skeet/lagcomp/update_timing.hpp"
#include <bit>

namespace skeet::lagcomp {
namespace {
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t absolute(std::int32_t value) noexcept {
    const auto sign = value < 0 ? UINT32_MAX : 0u;
    return std::bit_cast<std::int32_t>((std::uint32_t(value) ^ sign) - sign);
}
}
SimulationTickSpan simulation_tick_span(float previous, float current, float interval) noexcept {
    auto first = time_to_tick(previous, interval);
    const auto last = time_to_tick(current, interval);
    auto count = sub(last, first);
    if (count < 0)
        count = 0;
    else if (count > 16)
        count = 16;
    if (add(first, count) != last)
        first = sub(last, count);
    return {first, last, count};
}
SameTickRestoreWrites restore_same_tick_history_fields(const SameTickHistoryState& state,
                                                       std::int32_t receive, float interval,
                                                       float& history_time, float& player_time,
                                                       Vector& velocity) noexcept {
    SameTickRestoreWrites writes;
    if (state.simulation_override_receive_tick == receive &&
        absolute(sub(state.simulation_override_tick, receive)) <= 20) {
        const float time = float(sub(state.simulation_override_tick, 1)) * interval;
        history_time = time;
        player_time = time;
        writes.simulation = true;
    }
    if (state.velocity_receive_tick == receive) {
        velocity = state.velocity;
        writes.velocity = true;
    }
    return writes;
}
std::uint32_t initial_history_update_flags(const UpdateDiscontinuity& input) noexcept {
    std::uint32_t flags = input.saved_receive_tick_1a4 == input.receive_tick ||
                          (input.entity_effects_38ca5228 & 8u) ||
                          input.field_043fa575 != input.field_334b460c;
    if (input.velocity_receive_tick_1dc == input.next_receive_tick)
        flags |= 8u;
    return flags;
}
bool history_weapon_is_firearm(std::uint16_t identifier) noexcept {
    constexpr std::array<std::uint32_t, 3> mask{0xFF8B6F9Eu, 0xB00001DFu, 1};
    return identifier < 65u && (mask[identifier >> 5] & (1u << (identifier & 31u)));
}
std::int32_t update_history_shot_flags(std::uint32_t& owner, std::uint32_t& update,
                                       SimulationTickSpan span, HistoryWeaponShot weapon,
                                       float interval) noexcept {
    if (span.count <= 0)
        return -1;
    owner = (owner & 0xFFFFFFEFu) | ((owner >> 1) & 0x10u);
    if (weapon.available) {
        const auto tick = time_to_tick(weapon.time, interval);
        owner =
            (owner & 0xFFFFFFFEu) | std::uint32_t(tick == sub(span.last, 1) || tick == span.last);
        if (history_weapon_is_firearm(weapon.identifier) && tick > span.first &&
            tick <= span.last) {
            owner |= 0x20u;
            update |= 4u;
            return tick;
        }
    }
    owner &= 0xFFFFFFDFu;
    return -1;
}
void update_history_collision_transition(CollisionTransition& state, SimulationTickSpan span,
                                         float current_height, float origin_z, float previous_time,
                                         float interval) noexcept {
    if (state.received_updates_1d4 > 1 && span.count &&
        state.saved_collision_height_11c != current_height) {
        state.field_12c = origin_z + state.saved_collision_height_11c;
        state.field_148 = previous_time + interval;
    }
}
bool history_animation_layers_changed(
    const std::array<AnimationLayerState, 13>& current,
    const std::array<AnimationLayerState, 13>& previous) noexcept {
    for (std::size_t i = 0; i != 13; ++i)
        for (std::size_t j = 0; j != 7; ++j)
            if (current[i].words[j] != previous[i].words[j])
                return true;
    return false;
}
} // namespace skeet::lagcomp
