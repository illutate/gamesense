#include "skeet/movement/command_air.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/movement/air_feedback.hpp"
#include "skeet/movement/air_input.hpp"
#include "skeet/movement/air_steering.hpp"
#include "skeet/movement/air_velocity.hpp"
#include "skeet/movement/normalized_air_final.hpp"
#include "skeet/movement/normalized_air_wall.hpp"
#include "skeet/movement/normalized_scout_gate.hpp"
#include "skeet/movement/pre_speed_live.hpp"
#include "skeet/movement/raw_scout_gate.hpp"
#include "skeet/movement/z_hop.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
namespace {
using namespace movement_detail;
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool convar_self_integer(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1c);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}
ZHopSettings z_hop_settings(const MovementCommandOwner& owner) noexcept {
    return {owner.config.z_hop.value != 0, owner.config.config_2b499e77.value.binding,
            owner.config.config_3027dc11.value, owner.config.config_3127de82.value};
}
AirStrafeSelection select(MovementCommandOwner& owner, const client::Command& command,
                          const MovementCommandBindings& b, bool normalized_basis) {
    const AirStrafeSettings settings{owner.config.air_strafe_direction.value,
                                     owner.config.pre_speed.value != 0,
                                     owner.config.config_712c1936.value.binding};
    auto selection = select_air_strafe_modes(settings, b.input, false, command.movement);
    if (normalized_basis && selection.pre_speed_requested)
        selection.pre_speed_effective = !convar_self_integer(b.pre_speed_guard);
    return selection;
}

AirVelocityPreparation prepare_velocity(MovementCommandOwner& owner, void* player,
                                        MovementVector velocity, std::uint32_t initial_flags,
                                        const AirStrafeSelection& selection, UserCommand& command,
                                        const MovementCommandBindings& b) {
    const float stamina = field<float>(player, b.fields.stamina_2f33c681);
    MovementSimulationRules rules{};
    rules.gravity = movement_command_convar_float(b.simulation.convars.gravity);
    rules.interval_per_tick = *b.environment.interval;
    MovementPhysics physics{};
    physics.player_flags = initial_flags;
    bool cap = false;
    const float squared = velocity.y * velocity.y + velocity.x * velocity.x;
    if ((initial_flags & on_ground) && (command.buttons & jump_button) && !(4.0f > squared)) {
        if (stamina > 0.0f)
            rules.stamina_recovery =
                movement_command_convar_float(b.simulation.convars.stamina_recovery);
        cap = convar_self_integer(owner.jump_speed_cap);
        if (cap)
            physics.player_speed_limit =
                field<float>(player, b.environment.fields.speed_limit_3eb71843);
        physics.ducking = field<std::uint8_t>(player, b.environment.fields.ducking_6b1a59b5) != 0;
        if (!physics.ducking) {
            const auto flags = movement_command_player_flags(player, b);
            physics.player_flags = (initial_flags & ~2u) | (flags & 2u);
        }
        rules.jump_impulse = movement_command_convar_float(b.simulation.convars.jump_impulse);
    }
    return prepare_air_strafe_velocity(
        velocity, stamina, physics, rules, (selection.movement_key_mode & 0xffu) != 0,
        owner.config.bunny_hop.value != 0, cap, owner.side_magnitude, command);
}

bool pre_speed(MovementCommandOwner& owner, void* player, const MovementVector& origin,
               MovementVector velocity, const MovementVector& forecast_velocity, float speed,
               const MovementFeedbackHistory& history, MovementFeedback& feedback,
               client::Command& command, MovementAngleView angles,
               const MovementCommandBindings& b) {
    const LivePreSpeedInput input{owner.limits, velocity, forecast_velocity, origin,
                                  history.primary_heading};
    const LivePreSpeedBindings bindings{player, b.environment, b.simulation,
                                        b.fields.origin_a9ee6062, b.collision};
    angles.yaw = choose_live_pre_speed_heading(input, angles.yaw, bindings);
    const float ideal =
        minimum_number(owner.limits.maximum_angle, owner.limits.angle_scale / (speed + epsilon));
    return resolve_pre_speed_feedback(history.primary_heading, ideal, owner.brake_threshold,
                                      owner.side_magnitude, feedback, command.movement, angles);
}

void raw(MovementCommandOwner& owner, void* player, void* weapon, client::Command& command,
         MovementAngleView angles, const MovementFeedbackHistory& history,
         MovementFeedback& feedback, const MovementCommandBindings& b) {
    // The raw preamble retains the velocity address, then obtains the origin
    // reference before loading velocity/ground state and selecting the modes.
    const auto& live_velocity = *reinterpret_cast<const MovementVector*>(
        static_cast<const std::byte*>(player) + b.fields.velocity_36375c23);
    const auto& origin = movement_command_position_ref(player);
    const auto flags = movement_command_player_flags(player, b);
    if (feedback.original_buttons & speed_button)
        return;
    const auto selection = select(owner, command, b, false);
    if (!selection.continue_solver)
        return;
    auto velocity = live_velocity;
    const auto definition = weapon ? movement_command_weapon_definition(weapon, b) : 0u;
    const RawScoutGateFrame scout{velocity, flags, weapon != nullptr, std::int32_t(definition),
                                  origin};
    const RawScoutGateBindings scout_bindings{player, b.engine_client, b.collision,
                                              b.environment.engine_trace_flag};
    if (process_raw_scout_gate({owner.config.bunny_hop.value != 0, owner.side_magnitude}, selection,
                               scout, feedback, command.movement,
                               scout_bindings) != RawScoutGateResult::continue_air)
        return;
    // Native79B59 rereads only Z after the scout traces when cached XY speed
    // is zero. The XY values saved before those traces remain the heading seed.
    if (horizontal_length(velocity) == 0.0f)
        velocity.z = live_velocity.z;
    if (!prepare_air_strafe_heading(selection, velocity, false, command.movement, angles))
        return;
    const AirWallSteeringInput wall{
        owner.config.avoid_collisions.value != 0,
        owner.config.config_af9ae891.value,
        owner.limits.angle_scale,
        owner.limits.maximum_angle,
        owner.side_magnitude,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
        origin,
        velocity,
        flags,
        0};
    const AirWallSteeringLiveBindings wall_bindings{
        player,
        b.environment,
        b.simulation,
        b.collision,
        b.ballistics,
        b.surface_properties,
        origin,
        live_velocity,
        {owner.config.config_af9ae891.value, owner.config.air_strafe_smoothing.value,
         owner.limits.angle_scale, owner.limits.maximum_angle, owner.side_magnitude,
         owner.brake_threshold, owner.angle_step}};
    steer_live_air_wall(wall, feedback, command, angles, wall_bindings);
    const float speed = horizontal_length(velocity);
    const float ideal =
        minimum_number(owner.limits.maximum_angle, owner.limits.angle_scale / (speed + epsilon));
    const float velocity_yaw = planar_movement_yaw(velocity);
    auto smoothing = owner.config.air_strafe_smoothing.value;
    if (selection.pre_speed_requested) {
        if (!convar_self_integer(b.pre_speed_guard)) {
            if (pre_speed(owner, player, origin, velocity, live_velocity, speed, history, feedback,
                          command, angles, b))
                return;
            smoothing = 0;
        } else {
            smoothing = 100;
            apply_guarded_pre_speed_heading(speed, velocity_yaw, history.primary_heading,
                                            owner.limits.maximum_angle, angles);
        }
    } else if (apply_z_hop_heading(z_hop_settings(owner), b.input, b.engine_time, angles))
        smoothing = 0;
    AirSteeringState state{feedback.side_flip != 0, feedback.primary_heading};
    write_air_strafe_step({velocity_yaw, ideal, speed, owner.side_magnitude, smoothing,
                           (selection.movement_key_mode & 0xffu) != 0},
                          state, command.movement, angles);
    feedback.side_flip = std::uint8_t(state.side_flip);
    feedback.primary_heading = state.feedback_heading;
}

void normalized(MovementCommandOwner& owner, void* player, void* weapon, client::Command& command,
                MovementAngleView angles, const MovementFeedbackHistory& history,
                MovementFeedback& feedback, const MovementCommandBindings& b) {
    // Unlike the raw path, native7322A copies all three velocity components
    // before the player origin getter. Its ground byte is loaded afterwards.
    const auto original_velocity = movement_command_velocity(player, b);
    const auto& origin = movement_command_position_ref(player);
    const auto flags = movement_command_player_flags(player, b);
    if (feedback.original_buttons & speed_button)
        return;
    const auto selection = select(owner, command, b, true);
    if (!selection.continue_solver)
        return;
    const auto definition = weapon ? movement_command_weapon_definition(weapon, b) : 0u;
    if (!pass_normalized_scout_gate({player, b.engine_client, original_velocity, origin,
                                     weapon != nullptr, static_cast<std::uint16_t>(definition)},
                                    command, b.collision, b.environment.engine_trace_flag))
        return;
    const auto prepared =
        prepare_velocity(owner, player, original_velocity, flags, selection, command.movement, b);
    if (!prepared.continue_solver ||
        !prepare_air_strafe_heading(selection, prepared.velocity, true, command.movement, angles))
        return;
    const NormalizedAirWallInput wall{
        owner.config.avoid_collisions.value != 0,
        owner.config.config_af9ae891.value,
        owner.config.air_strafe_smoothing.value,
        owner.limits.maximum_angle,
        owner.side_magnitude,
        owner.brake_threshold,
        owner.angle_step,
        0.0f,
        selection.pre_speed_effective,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
        origin,
        prepared.velocity,
        flags,
        0};
    const AirWallSteeringLiveBindings wall_bindings{
        player,
        b.environment,
        b.simulation,
        b.collision,
        b.ballistics,
        b.surface_properties,
        origin,
        prepared.velocity,
        {owner.config.config_af9ae891.value, owner.config.air_strafe_smoothing.value,
         owner.limits.angle_scale, owner.limits.maximum_angle, owner.side_magnitude,
         owner.brake_threshold, owner.angle_step}};
    const float velocity_yaw = steer_live_normalized_air_wall(wall, feedback, command, angles,
                                                              wall_bindings, owner.air_accelerate);
    const float speed = horizontal_length(prepared.velocity);
    if (selection.pre_speed_requested) {
        if (selection.pre_speed_effective &&
            pre_speed(owner, player, origin, prepared.velocity, prepared.velocity, speed, history,
                      feedback, command, angles, b))
            return;
    } else
        apply_z_hop_heading(z_hop_settings(owner), b.input, b.engine_time, angles);
    const NormalizedAirFinalSettings settings{owner.config.air_strafe_smoothing.value,
                                              owner.limits.maximum_angle, owner.side_magnitude,
                                              owner.brake_threshold, owner.angle_step};
    const NormalizedAirFinalInput input{origin, prepared.velocity, prepared.stamina, velocity_yaw,
                                        speed,  selection};
    const NormalizedAirFinalBindings bindings{
        player,       b.environment,        b.simulation,        b.collision,
        b.ballistics, b.surface_properties, owner.air_accelerate};
    finish_normalized_air_strafe(settings, input, history, feedback, command, angles, bindings);
}
}

void process_live_movement_air(MovementCommandOwner& owner, void* player, void* weapon,
                               client::Command& command, MovementAngleView angles,
                               const MovementFeedbackHistory& history, MovementFeedback& feedback,
                               const MovementCommandBindings& bindings) {
    if (owner.config.config_ad536de0.value)
        normalized(owner, player, weapon, command, angles, history, feedback, bindings);
    else
        raw(owner, player, weapon, command, angles, history, feedback, bindings);
}

} // namespace skeet
