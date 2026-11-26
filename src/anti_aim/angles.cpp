#include "skeet/anti_aim/angles.hpp"

namespace skeet::anti_aim {
template <class Bindings>
void angle_stage(const GlobalConfig& global, const Config& config,
                 const AngleParameters& parameters, AngleStageState& state, AngleFrame& frame,
                 const AnglePlayer& player, ActionGateGlobals& globals, client::Command& command,
                 std::uint8_t& send_packet, const Bindings& bindings) {
    ActionGateState gates{state.flags_130, state.value_158};
    const auto path = evaluate_action_gates(
        {global.enabled.value != 0, global.pitch.value, config.yaw_base.value}, frame.gates,
        player.gates, globals, gates, command, send_packet, bindings.proximity);
    state.flags_130 = gates.flags_130;
    state.value_158 = gates.sequence_158;
    if (path == ActionGatePath::stop)
        return;
    if (path == ActionGatePath::cached_yaw) {
        CommandYawState tail{state.flags_130, state.cached_yaw_18c};
        apply_cached_command_yaw(command.angles[1], tail);
        state.cached_yaw_18c = tail.cached_yaw_18c;
        return;
    }
    apply_pitch(
        command.angles[0],
        {global.pitch.value, config.custom_pitch.value, parameters.value_138, parameters.value_150},
        {frame.gates.flags, command.movement.buttons, player.gates.movement_type_8b5586f6,
         command.number, frame.angle_9c, player.gates.has_weapon_3d52a1a7, player.weapon_pitch_340},
        bindings.pitch);
    if ((!config.freestanding.value || (state.value_1ac & 0x618u)) && !(state.flags_130 & 8u)) {
        state.value_1ac = 0;
        state.value_1a8 = std::numeric_limits<float>::max();
    }
    const bool edge = solve_edge_yaw({config.edge_yaw.value, frame.scalar_c0,
                                      player.edge_scalar_3eb71843, player.state_7501cb91},
                                     {parameters.value_148, parameters.value_14c}, bindings.edge,
                                     bindings.trace_flag, command.angles[1]);
    if (edge) {
        state.value_1a8 = std::numeric_limits<float>::max();
        frame.gates.flags |= 0x80000u;
    } else if (global.yaw.value != 0 || global.body_yaw.value != 0) {
        if (global.yaw.value != 0)
            apply_target_yaw_base(config.yaw_base.value,
                                  {globals.target_handle, bindings.freestanding.target[0],
                                   bindings.freestanding.target[1]},
                                  frame.origin_dc[0], frame.origin_dc[1], globals.entities,
                                  command.angles[1]);
        auto free_context = bindings.freestanding;
        free_context.flags = globals.command_flags;
        free_context.target_handle = globals.target_handle;
        free_context.entities = globals.entities;
        FreestandingState free_state{state.flags_130, state.value_1a8, state.value_1ac};
        const auto free = apply_freestanding(
            {config.freestanding.value != 0, config.freestanding_hotkey.value.binding},
            {frame.scalar_c0, frame.scalar_38, frame.height_164, command.movement.forward_move,
             frame.origin_10c, frame.origin_168},
            free_context, free_state, command.angles[1], bindings.ballistics,
            bindings.body.workspace.freestanding);
        state.value_1a8 = free_state.yaw_1a8;
        state.value_1ac = free_state.value_1ac;
        if (!free)
            command.angles[1] = transform_yaw(
                command.angles[1], float(config.yaw_offsets.value[global.yaw.value]),
                global.yaw.value, state.saved_yaw_198, {parameters.value_138, parameters.value_148},
                {frame.angle_b8, frame.origin_dc[0], frame.origin_dc[1], frame.interpolation_158},
                {globals.target_handle, free_context.target[0], free_context.target[1]},
                globals.entities);
    }
    if (frame.gates.choked_15c == 0)
        send_packet = 0;
    if (!(state.flags_130 & 8u)) {
        apply_yaw_jitter(command.angles[1], global.yaw.value,
                         {config.yaw_jitter.value, config.yaw_jitter_offset.value,
                          parameters.value_14c, parameters.value_148, parameters.pattern_15c},
                         state.value_190, command.number, bindings.random);
        if (!(frame.gates.flags & 0x40u) &&
            lag_step_pending(frame.gates.flags, frame.gates.choke_limit_fc,
                             frame.gates.choked_15c) &&
            command.number != bindings.last_sent_command && global.body_yaw.value != 0) {
            // Working 4332E8C3 skips the shot-position correction.
            const auto base_yaw = command.angles[1];
            float minimum, maximum;
            animation_yaw_limits(player.animation, minimum, maximum);
            BodyYawState body_state{state.value_190, state.value_1a0, state.reference_yaw_258};
            const auto offset =
                solve_body_yaw({global.body_yaw.value, config.yaw_jitter.value,
                                config.body_yaw_offsets.value[2], config.body_yaw_offsets.value[3],
                                parameters.value_134, parameters.value_144, parameters.pattern_15c},
                               body_state,
                               {config.freestanding_body_yaw.value != 0, frame.command_ac,
                                bindings.last_body_command, bindings.shot_command, frame.origin_dc,
                                command.angles[0], base_yaw, maximum},
                               bindings.body, bindings.random);
            state.value_1a0 = body_state.cached_opposite_1a0;
            blend_body_yaw({parameters.value_134, parameters.value_138, parameters.value_13c,
                            parameters.value_144, parameters.value_154},
                           {offset, minimum, maximum, base_yaw, player.feet_yaw_80,
                            frame.gates.choked_15c, frame.gates.choke_limit_fc},
                           command.angles[1], send_packet);
        }
    }
    CommandYawState tail{state.flags_130, state.cached_yaw_18c};
    finalize_command_yaw(command.angles[1], frame.gates.yaw_24, tail);
    state.cached_yaw_18c = tail.cached_yaw_18c;
}
void process_angle_stage(const GlobalConfig& global, const Config& config,
                         const AngleParameters& parameters, AngleStageState& state,
                         AngleFrame& frame, const AnglePlayer& player, ActionGateGlobals& globals,
                         client::Command& command, std::uint8_t& send_packet,
                         const AngleStageBindings& bindings) {
    angle_stage(global, config, parameters, state, frame, player, globals, command, send_packet,
                bindings);
}
void process_angle_stage(const GlobalConfig& global, const Config& config,
                         const AngleParameters& parameters, AngleStageState& state,
                         AngleFrame& frame, const AnglePlayer& player, ActionGateGlobals& globals,
                         client::Command& command, std::uint8_t& send_packet,
                         const LiveAngleStageBindings& bindings) {
    angle_stage(global, config, parameters, state, frame, player, globals, command, send_packet,
                bindings);
}
} // namespace skeet::anti_aim
