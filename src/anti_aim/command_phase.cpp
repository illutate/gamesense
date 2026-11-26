#include "skeet/anti_aim/command_phase.hpp"

namespace skeet::anti_aim {
void process_command_angles(const GlobalConfig& global, const Config& config,
                            const AngleParameters& parameters, AngleStageState& state,
                            AngleFrame& frame, const AnglePlayer& player,
                            ActionGateGlobals& globals, client::Command& command,
                            std::uint8_t& send_packet, TargetState& target,
                            const CommandPhaseInput& input, const CommandPhaseBindings& bindings) {
    if (!command_eligible({global.enabled.value != 0, global.pitch.value, global.yaw.value,
                           global.body_yaw.value},
                          {frame.gates.flags, input.secondary_frame_flags}, input.guard,
                          state.value_1c4, bindings.eligibility, state.flags_130))
        return;

    const auto cached = command_uses_cached_angles(
        input.player_a15c3105, player.gates.movement_type_8b5586f6, frame.gates.flags,
        command.movement.buttons, bindings.angles.proximity, player.gates.entity);
    AngleCycleState cycle{state.flags_130, state.value_190, state.saved_yaw_198};
    begin_angle_cycle(cached, frame.gates.choked_15c, config.yaw_offsets.value[3], cycle);
    state.flags_130 = cycle.flags_130;
    state.value_190 = cycle.phase_190;
    state.saved_yaw_198 = cycle.saved_yaw_198;

    if (config.yaw_base.value == 1 || config.freestanding.value ||
        config.freestanding_body_yaw.value)
        refresh_yaw_target(target, bindings.target_refresh);
    globals.target_handle = target.handle;

    // All three native consumers read the same target publication. Retain the
    // shot position from434759D0 independently of the target's current origin.
    auto freestanding = bindings.angles.freestanding;
    freestanding.target_handle = target.handle;
    freestanding.target = target.position;
    auto body_context = bindings.angles.body.context;
    body_context.target_handle = target.handle;
    body_context.target_origin = target.position;
    body_context.entities = globals.entities;
    const BodyYawFreestandingBindings body{{config.yaw_base.value, parameters.value_148},
                                           body_context,
                                           bindings.angles.body.bones,
                                           bindings.angles.body.math,
                                           bindings.angles.body.clipper,
                                           bindings.angles.body.workspace};
    const AngleStageBindings stage{bindings.angles.proximity,
                                   bindings.angles.edge,
                                   bindings.angles.ballistics,
                                   freestanding,
                                   body,
                                   bindings.angles.pitch,
                                   bindings.angles.random,
                                   bindings.angles.trace_flag,
                                   bindings.angles.last_body_command,
                                   bindings.angles.shot_command,
                                   bindings.angles.last_sent_command,
                                   bindings.angles.shot_correction};
    process_angle_stage(global, config, parameters, state, frame, player, globals, command,
                        send_packet, stage);
}
} // namespace skeet::anti_aim
