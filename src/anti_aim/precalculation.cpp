#include "skeet/anti_aim/precalculation.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <cstring>
#include <type_traits>

namespace skeet::anti_aim {
namespace {
template <class T> T live_field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
void apply_micro_movement(PrecalculationState& state, const PrecalculationPlayer& player,
                          float interval, client::Command& command) noexcept {
    const auto original = state.micro_move_19c;
    auto amount = original;
    auto duck = player.duck_3309c0cf;
    bool scale;
    if (command.movement.buttons & 4u) {
        const auto advanced = (interval * 0.8f) * player.rate_fb7e3404 + duck;
        duck = 1.0f < advanced ? 1.0f : advanced; // native MINSS selects second on unordered
        scale = true;
    } else {
        if (duck != 1.0f || duck != state.previous_duck_1a4) {
            const auto advanced = duck - maximum_number(1.5f, player.rate_fb7e3404) * interval;
            duck = 0.0f > advanced ? 0.0f : advanced; // native MAXSS, including signed zero
        }
        scale = player.field_6b1a59b5 || (player.flags_e2fbe695 & 2u);
    }
    if (scale)
        amount /= ((duck * 0.34f + 1.0f) - duck);
    state.micro_move_19c =
        std::bit_cast<float>(std::bit_cast<std::uint32_t>(original) ^ 0x80000000u);
    const auto forward = command.movement.forward_move;
    if (forward != 0.0f) {
        if (forward < 0.0f)
            amount = forward - amount;
        else
            amount += forward;
    }
    command.movement.forward_move = amount;
}
template <class Bindings>
void precalculate_angles(const GlobalConfig& global, const Config& config,
                         PrecalculationState& state, const PrecalculationPlayer& player,
                         std::int32_t choked_commands, client::Command& command,
                         const std::array<float, 3>& incoming_angles, std::uint8_t& send_packet,
                         const Bindings& bindings) {
    constexpr bool live = std::is_same_v<Bindings, LivePrecalculationBindings>;
    auto& common = state.common;
    common.flags_130 &= ~2u;
    if (!global.enabled.value)
        return;
    common.flags_130 = (common.flags_130 & ~1u) | (player.flags_e2fbe695 & 1u);
    if (choked_commands == 0 && player.has_animation_state)
        state.feet_yaw_194 = player.feet_yaw_80;
    if ((global.body_yaw.value != 0 || config.roll.value != 0) && !player.guard_d720aea6 &&
        !(common.flags_130 & 4u) && !(command.movement.buttons & 0x63Au) &&
        player.movement_type_7501cb91 == 2) {
        const auto origin = *engine::invoke<const MovementVector*>(bindings.player, 10);
        if constexpr (live) {
            initialize_live_movement_simulation(bindings.workspace, bindings.player, origin,
                                                player.velocity_36375c23, player.flags_e2fbe695, 0,
                                                bindings.movement);
        } else
            initialize_movement_simulation(bindings.workspace, bindings.player_value, origin,
                                           player.velocity_36375c23, player.flags_e2fbe695, 0,
                                           bindings.movement_player, bindings.movement_rules);
        const auto saved = command.angles;
        command.angles = incoming_angles;
        if constexpr (live)
            simulate_live_movement_tick(bindings.workspace, bindings.collision,
                                        bindings.environment, bindings.player, command);
        else
            simulate_movement_tick(bindings.workspace, bindings.collision,
                                   bindings.movement_environment, bindings.player_value,
                                   command.movement,
                                   {command.angles[0], command.angles[1], command.angles[2]});
        command.angles = saved;
        const auto& velocity = bindings.workspace.velocity;
        const auto speed = std::sqrt((velocity.x * velocity.x + velocity.z * velocity.z) +
                                     velocity.y * velocity.y);
        if (speed < 2.0f) {
            auto choked = choked_commands;
            if constexpr (live)
                choked = **bindings.fields.choked;
            if (choked == 0)
                send_packet = 0;
            if (global.body_yaw.value != 0) {
                auto current = player;
                float interval;
                if constexpr (live) {
                    const auto& offsets = bindings.fields;
                    current.duck_3309c0cf =
                        live_field<float>(bindings.player, offsets.duck_3309c0cf);
                    current.rate_fb7e3404 =
                        live_field<float>(bindings.player, offsets.rate_fb7e3404);
                    current.field_6b1a59b5 =
                        live_field<std::uint8_t>(bindings.player, offsets.field_6b1a59b5) != 0;
                    current.flags_e2fbe695 =
                        live_field<std::uint32_t>(bindings.player, offsets.flags_e2fbe695);
                    interval = **offsets.interval;
                } else
                    interval = bindings.movement_rules.interval_per_tick;
                apply_micro_movement(state, current, interval, command);
            }
            if (config.roll.value != 0 && !client::command_roll_blocked(bindings.roll_guard) &&
                !(command.movement.buttons & 0x801u))
                command.angles[2] = static_cast<float>(config.roll.value);
        }
    }
    if (common.value_1a8 != std::numeric_limits<float>::max())
        common.value_1ac |= command.movement.buttons;
    if constexpr (live)
        state.previous_duck_1a4 = live_field<float>(bindings.player, bindings.fields.duck_3309c0cf);
    else
        state.previous_duck_1a4 = player.duck_3309c0cf;
}
void precalculate_command_angles(const GlobalConfig& global, const Config& config,
                                 PrecalculationState& state, const PrecalculationPlayer& player,
                                 std::int32_t choked, client::Command& command,
                                 const std::array<float, 3>& incoming, std::uint8_t& send,
                                 const PrecalculationBindings& bindings) {
    precalculate_angles(global, config, state, player, choked, command, incoming, send, bindings);
}
void precalculate_live_command_angles(const GlobalConfig& global, const Config& config,
                                      PrecalculationState& state,
                                      const PrecalculationPlayer& player, std::int32_t choked,
                                      client::Command& command,
                                      const std::array<float, 3>& incoming, std::uint8_t& send,
                                      const LivePrecalculationBindings& bindings) {
    precalculate_angles(global, config, state, player, choked, command, incoming, send, bindings);
}
} // namespace skeet::anti_aim
