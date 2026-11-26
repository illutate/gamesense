#include "skeet/movement/command_runtime.hpp"
#include "skeet/movement/command_air.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/movement/ground_phase_live.hpp"
#include "skeet/movement/player_air_gate.hpp"
#include "skeet/movement/weapon_speed_phase.hpp"

namespace skeet {
namespace {
void process(MovementCommandOwner& owner, void* player, void* weapon, client::Command& command,
             MovementAngleView angles, std::uint8_t& send_packet, MovementFeedback& feedback,
             const MovementCommandBindings& bindings) {
    const auto history = begin_movement_command(
        feedback, command.movement, owner.duck_peek.original_buttons, bindings.scheduler_flags);
    process_live_duck_peek(owner, player, weapon, command.movement, send_packet, bindings);
    if (weapon && process_live_quick_peek(owner.quick_peek_config, bindings.input, owner.quick_peek,
                                          player, weapon, command, feedback.original_buttons,
                                          angles, bindings.scheduler_flags,
                                          bindings) == QuickPeekContinuation::complete)
        return;

    const auto ground = process_live_ground_phase(owner, player, weapon, command, angles, bindings);
    if (ground == GroundPhaseContinuation::complete)
        return;
    if (ground == GroundPhaseContinuation::weapon_speed_features &&
        process_live_weapon_speed_phase(player, weapon, command, angles, bindings) ==
            WeaponSpeedPhaseContinuation::complete)
        return;

    const PlayerAirGateSettings settings{owner.config.air_strafe.value != 0,
                                         owner.config.bunny_hop.value != 0,
                                         owner.side_magnitude,
                                         owner.brake_reduction_limit,
                                         owner.brake_threshold,
                                         owner.air_accelerate};
    const PlayerAirGateFrame frame{player, weapon, &feedback};
    const auto player_bindings = capture_player_air_gate_bindings(bindings);
    const auto gate = process_player_air_gate(settings, frame, player_bindings,
                                              bindings.scheduler_flags, command.movement, angles);
    if (gate.continuation == PlayerAirGateContinuation::complete)
        return;
    if (gate.continuation == PlayerAirGateContinuation::air_solver && gate.air_strafe_enabled)
        process_live_movement_air(owner, player, weapon, command, angles, history, feedback,
                                  bindings);
    process_live_ground_speed_tail(owner, player, command, angles, bindings);
}
}

void process_movement_command(MovementCommandOwner& owner, void* player, void* weapon,
                              client::Command& command, MovementAngles& angles,
                              std::uint8_t& send_packet, MovementFeedback& feedback,
                              const MovementCommandBindings& bindings) {
    process(owner, player, weapon, command, MovementAngleView{angles}, send_packet, feedback,
            bindings);
}

void process_movement_command(MovementCommandOwner& owner, void* player, void* weapon,
                              client::Command& command, std::array<float, 3>& angles,
                              std::uint8_t& send_packet, MovementFeedback& feedback,
                              const MovementCommandBindings& bindings) {
    process(owner, player, weapon, command, MovementAngleView{angles}, send_packet, feedback,
            bindings);
}

} // namespace skeet
