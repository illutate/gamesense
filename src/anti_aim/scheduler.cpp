#include "skeet/anti_aim/scheduler.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace skeet::anti_aim {
namespace {
float normalize_yaw(float yaw) noexcept {
    if (std::isnan(yaw))
        return 0.0f;
    const auto absolute = std::bit_cast<float>(std::bit_cast<std::uint32_t>(yaw) & 0x7FFFFFFFu);
    if (absolute > 180.0f) {
        const auto turns =
            std::bit_cast<float>(std::bit_cast<std::uint32_t>(yaw / 360.0f) & 0x7FFFFFFFu);
        const auto correction = round_movement_step(turns) * 360.0f;
        if (yaw < 0.0f)
            yaw += correction;
        else
            yaw -= correction;
    }
    return yaw;
}
}
void finalize_command_yaw(float& command_yaw, float frame_yaw_24, CommandYawState& state) noexcept {
    auto current = command_yaw;
    if (frame_yaw_24 != current) {
        current = normalize_yaw(current);
        command_yaw = current;
    }
    apply_cached_command_yaw(command_yaw, state);
}
void apply_cached_command_yaw(float& command_yaw, CommandYawState& state) noexcept {
    if (!(state.flags_130 & 8u))
        return;
    if (state.cached_yaw_18c != std::numeric_limits<float>::max())
        command_yaw = state.cached_yaw_18c;
    else
        state.cached_yaw_18c = normalize_yaw(command_yaw);
}
void update_command_flags(std::uint32_t frame_flags, std::uint32_t& flags_130) noexcept {
    flags_130 = (flags_130 & ~4u) | ((frame_flags >> 4) & 4u);
}
void begin_angle_cycle(bool cached_command, std::int32_t choked_commands, std::int32_t spin_offset,
                       AngleCycleState& state) noexcept {
    state.flags_130 = (state.flags_130 & ~8u) | (cached_command ? 8u : 0u);
    if (choked_commands != 0 || cached_command)
        return;
    const auto speed = static_cast<float>(spin_offset);
    const auto updated = normalize_yaw(speed + state.saved_yaw_198);
    ++state.phase_190;
    state.saved_yaw_198 = updated;
}
ActionGatePath evaluate_action_gates(const ActionGateSettings& settings,
                                     const ActionGateFrame& frame, const ActionGatePlayer& player,
                                     ActionGateGlobals& globals, ActionGateState& state,
                                     client::Command& command, std::uint8_t& send_packet,
                                     ProximityEngine& proximity) {
    const auto stop = ActionGatePath::stop;
    if (frame.state_104 == 8)
        return stop;
    const auto kind = frame.weapon_kind_124;
    auto& buttons = command.movement.buttons;
    if (kind == 9 || globals.mode_43475b2c == state.sequence_158) {
        if (!player.weapon_state_e1df7e9b && player.weapon_time_3b93667b > 0.0f &&
            static_cast<float>(player.tick_base_3d10b94c) * globals.interval_per_tick >
                player.weapon_time_3b93667b) {
            if (globals.choked_434757dc == 0)
                send_packet = 0;
            state.sequence_158 = globals.mode_43475b2c;
            return stop;
        }
    } else if (kind != 7 &&
               ((frame.flags & 0x100000u) || ((frame.flags & 0x400u) && (buttons & 1u)) ||
                ((frame.flags & 2u) && (buttons & 0x800u) &&
                 (frame.weapon_id_d8 == 0x40 || kind == 0)))) {
        if (kind == 0 && frame.choked_15c < frame.choke_limit_fc) {
            send_packet = 0;
            globals.command_flags = (globals.command_flags & ~0x400u) | 0x200u;
        }
        return stop;
    }
    if (frame.state_104 == 9 && (buttons & 0x618u)) {
        if (frame.choke_limit_fc <= 6 || !settings.enabled || !settings.pitch_mode ||
            (globals.command_flags & 0x600u))
            return stop;
        if (frame.choked_15c < frame.choke_limit_fc) {
            send_packet = 0;
            return stop;
        }
        buttons &= ~0x618u;
    }
    if (settings.yaw_base == 1) {
        const bool found =
            globals.target_handle != -1 &&
            globals.entities.by_handle(globals.entities.object, globals.target_handle);
        if (!found && (frame.flags & 8u))
            return stop;
    }
    if ((frame.flags & 0x40u) && kind != 7 && (frame.previous_buttons_44 & 0x801u)) {
        if ((frame.previous_buttons_44 & 0x801u) != 0x800u || kind == 0 ||
            frame.weapon_id_d8 == 0x40)
            return stop;
    }
    if (buttons & 0x20u) {
        if (player.movement_type_8b5586f6 == 3) {
            if (!first_nearby_entity(proximity, player.entity))
                return stop;
        } else if (frame.weapon_id_d8 != 0x31 || !player.field_7fcb7381)
            return stop;
    }
    if (command.angles[0] != frame.pitch_20 || command.angles[1] != frame.yaw_24)
        return stop;
    if (!player.has_weapon_3d52a1a7)
        return ActionGatePath::cached_yaw;
    state.flags_130 |= 2u;
    return ActionGatePath::angles;
}
} // namespace skeet::anti_aim
