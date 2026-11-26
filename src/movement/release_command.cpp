#include "skeet/movement/release_command.hpp"
#include "skeet/movement/air_feedback.hpp"
#include "skeet/movement/air_input.hpp"
#include "skeet/movement/air_steering.hpp"
#include "skeet/movement/air_velocity.hpp"
#include "skeet/movement/normalized_air_wall.hpp"
#include "skeet/movement/normalized_scout_gate.hpp"
#include "skeet/movement/pre_speed_live.hpp"
#include "skeet/movement/raw_scout_gate.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cstring>
#include <limits>

namespace skeet {
namespace {
using namespace movement_detail;

template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            return object;
        object = parent;
    }
}
bool convar_self_integer(const void* object) noexcept {
    object = convar_parent(object);
    return field<std::uint32_t>(object, 0x30u) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}
float convar_float(const void* object) noexcept {
    object = convar_parent(object);
    return std::bit_cast<float>(
        field<std::uint32_t>(object, 0x2Cu) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
PreSpeedLimits limits(const MovementReleaseRuntime& runtime) noexcept {
    return {runtime.tuning.angle_scale, runtime.tuning.maximum_angle};
}
ZHopSettings z_hop(const MovementReleaseRuntime& runtime) noexcept {
    const auto& config = runtime.config;
    return {config.z_hop.value != 0, config.config_2b499e77.value.binding,
            config.config_3027dc11.value, config.config_3127de82.value};
}
AirStrafeSelection select_modes(const MovementReleaseRuntime& runtime, bool normalized,
                                const UserCommand& command) {
    const auto direction = static_cast<std::uint32_t>(runtime.config.air_strafe_direction.value);
    AirStrafeSelection result{true, (direction & 1u) != 0, (direction >> 1) & 0xFFFFFF01u, false,
                              false};
    result.pre_speed_requested =
        runtime.config.pre_speed.value &&
        hotkey_active(runtime.config.config_712c1936.value.binding, runtime.input);
    if (normalized && result.pre_speed_requested)
        result.pre_speed_effective = !convar_self_integer(runtime.pre_speed_guard);
    const bool no_input = command.side_move == 0.0f && command.forward_move == 0.0f;
    if ((result.movement_key_mode & 0xFFu) && !result.view_angles && !result.pre_speed_requested &&
        no_input)
        result.continue_solver = false;
    if (no_input)
        result.movement_key_mode &= 0xFFFFFF00u;
    return result;
}
AirVelocityPreparation prepare_live_velocity(MovementReleaseRuntime& runtime, void* player,
                                             const MovementSimulation& simulation,
                                             const AirStrafeSelection& selection,
                                             UserCommand& command) {
    auto velocity = simulation.velocity;
    float stamina = field<float>(player, runtime.capture.offsets.stamina_2f33c681);
    velocity.z -=
        (convar_float(runtime.capture.convars.gravity) * 0.5f) * (*runtime.environment.interval);
    if (!(simulation.player_flags & on_ground))
        return {velocity, stamina, true};
    if (!(command.buttons & jump_button))
        return {velocity, stamina, false};
    if (4.0f > velocity.y * velocity.y + velocity.x * velocity.x) {
        if (!(selection.movement_key_mode & 0xFFu) && runtime.config.bunny_hop.value)
            command.forward_move = runtime.tuning.side_magnitude;
        return {velocity, stamina, false};
    }
    if (stamina > 0.0f)
        stamina = sse_max(0.0f, stamina - convar_float(runtime.capture.convars.stamina_recovery) *
                                              (*runtime.environment.interval));
    if (convar_self_integer(runtime.jump_speed_cap)) {
        const float limit =
            field<float>(player, runtime.environment.fields.speed_limit_3eb71843) * 1.1f;
        const float speed = std::sqrt((velocity.x * velocity.x + velocity.y * velocity.y) +
                                      velocity.z * velocity.z);
        if (speed > limit) {
            const float scale = limit / speed;
            velocity.z *= scale;
            velocity.x *= scale;
            velocity.y *= scale;
        }
    }
    const auto& fields = runtime.environment.fields;
    const bool duck = field<std::uint8_t>(player, fields.ducking_6b1a59b5) != 0 ||
                      (field<std::uint8_t>(player, fields.player_flags_e2fbe695) & 2u);
    const float impulse = convar_float(runtime.capture.convars.jump_impulse);
    if (duck)
        velocity.z = impulse;
    else
        velocity.z += impulse;
    if (stamina > 0.0f)
        velocity.z *= sse_min(sse_max(1.0f - stamina / 100.0f, 0.0f), 1.0f);
    velocity.z -=
        (convar_float(runtime.capture.convars.gravity) * 0.5f) * (*runtime.environment.interval);
    return {velocity, stamina, true};
}
AirWallSteeringLiveBindings wall_bindings(MovementReleaseRuntime& runtime, void* player,
                                          const MovementVector& origin,
                                          const MovementVector& velocity) {
    return {player,
            runtime.environment,
            runtime.capture,
            runtime.collision,
            runtime.ballistics,
            runtime.surface_properties,
            origin,
            velocity,
            {runtime.config.config_af9ae891.value, runtime.config.air_strafe_smoothing.value,
             runtime.tuning.angle_scale, runtime.tuning.maximum_angle,
             runtime.tuning.side_magnitude, runtime.tuning.feedback_turn_limit,
             runtime.tuning.normalized_angle_step}};
}
float pre_speed_heading(MovementReleaseRuntime& runtime, void* player,
                        const MovementSimulation& simulation, MovementVector velocity,
                        const MovementVector& forecast_velocity, float previous, float heading) {
    const LivePreSpeedInput input{limits(runtime), velocity, forecast_velocity, simulation.position,
                                  previous};
    const LivePreSpeedBindings bindings{player, runtime.environment, runtime.capture,
                                        runtime.offsets.forecast_position_a9ee6062,
                                        runtime.collision};
    return choose_live_pre_speed_heading(input, heading, bindings);
}

void synthesize_normalized(MovementReleaseRuntime& runtime, void* player, void* weapon,
                           const MovementSimulation& simulation,
                           const MovementFeedbackHistory& history,
                           const AirStrafeSelection& selection, MovementFeedback& feedback,
                           client::Command& command, MovementAngleView angles) {
    const auto definition =
        weapon ? field<std::uint16_t>(weapon, runtime.offsets.weapon_definition_5c9f0fbb)
               : std::uint16_t(0);
    if (!pass_normalized_scout_gate({player, runtime.engine_client, simulation.velocity,
                                     simulation.position, weapon != nullptr, definition},
                                    command, runtime.collision,
                                    runtime.environment.engine_trace_flag))
        return;
    const auto prepared =
        prepare_live_velocity(runtime, player, simulation, selection, command.movement);
    if (!prepared.continue_solver ||
        !prepare_air_strafe_heading(selection, prepared.velocity, true, command.movement, angles))
        return;
    const auto& tuning = runtime.tuning;
    const NormalizedAirWallInput wall{
        runtime.config.avoid_collisions.value != 0,
        runtime.config.config_af9ae891.value,
        runtime.config.air_strafe_smoothing.value,
        tuning.maximum_angle,
        tuning.side_magnitude,
        tuning.feedback_turn_limit,
        tuning.normalized_angle_step,
        0.0f,
        selection.pre_speed_effective,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
        simulation.position,
        prepared.velocity,
        0,
        -1};
    const float velocity_yaw = steer_live_normalized_air_wall(
        wall, feedback, command, angles,
        wall_bindings(runtime, player, simulation.position, prepared.velocity),
        runtime.air_acceleration);
    const float speed = horizontal_length(prepared.velocity);
    if (selection.pre_speed_requested) {
        if (selection.pre_speed_effective) {
            angles.yaw = pre_speed_heading(runtime, player, simulation, prepared.velocity,
                                           prepared.velocity, history.primary_heading, angles.yaw);
            const float ideal_step =
                minimum_number(tuning.maximum_angle, tuning.angle_scale / (speed + epsilon));
            if (resolve_pre_speed_feedback(history.primary_heading, ideal_step,
                                           tuning.feedback_turn_limit, tuning.side_magnitude,
                                           feedback, command.movement, angles))
                return;
        }
    } else {
        apply_z_hop_heading(z_hop(runtime), runtime.input, runtime.engine_time, angles);
    }
    // Advanced steering always retains its configured smoothing. The raw
    // solver's 100/0 overrides and guarded-heading mutation do not occur here.
    const NormalizedAirFinalSettings final_settings{
        runtime.config.air_strafe_smoothing.value, tuning.maximum_angle, tuning.side_magnitude,
        tuning.feedback_turn_limit, tuning.normalized_angle_step};
    const NormalizedAirFinalInput final_input{
        simulation.position, prepared.velocity, prepared.stamina, velocity_yaw, speed, selection};
    const NormalizedAirFinalBindings final_bindings{player,
                                                    runtime.environment,
                                                    runtime.capture,
                                                    runtime.collision,
                                                    runtime.ballistics,
                                                    runtime.surface_properties,
                                                    runtime.air_acceleration};
    finish_normalized_air_strafe(final_settings, final_input, history, feedback, command, angles,
                                 final_bindings);
}

void synthesize_raw(MovementReleaseRuntime& runtime, void* player, void* weapon,
                    const MovementSimulation& simulation, const MovementFeedbackHistory& history,
                    AirStrafeSelection selection, MovementFeedback& feedback,
                    client::Command& command, MovementAngleView angles) {
    auto velocity = simulation.velocity;
    const float speed = horizontal_length(velocity);
    const bool scout_reached = !(simulation.player_flags & on_ground) ||
                               ((command.movement.buttons & jump_button) && 2.0f > speed);
    const auto definition =
        scout_reached && weapon && 2.0f > speed
            ? field<std::uint16_t>(weapon, runtime.offsets.weapon_definition_5c9f0fbb)
            : std::uint16_t(0);
    const RawScoutGateFrame scout{velocity, simulation.player_flags, weapon != nullptr, definition,
                                  simulation.position};
    const RawScoutGateBindings scout_bindings{player, runtime.engine_client, runtime.collision,
                                              runtime.environment.engine_trace_flag};
    if (process_raw_scout_gate({runtime.config.bunny_hop.value != 0, runtime.tuning.side_magnitude},
                               selection, scout, feedback, command.movement,
                               scout_bindings) != RawScoutGateResult::continue_air)
        return;
    if (speed == 0.0f)
        velocity.z = simulation.velocity.z;
    if (!prepare_air_strafe_heading(selection, velocity, false, command.movement, angles))
        return;
    const auto& tuning = runtime.tuning;
    const AirWallSteeringInput wall{
        runtime.config.avoid_collisions.value != 0,
        runtime.config.config_af9ae891.value,
        tuning.angle_scale,
        tuning.maximum_angle,
        tuning.side_magnitude,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
        simulation.position,
        velocity,
        0,
        -1};
    steer_live_air_wall(wall, feedback, command, angles,
                        wall_bindings(runtime, player, simulation.position, simulation.velocity));
    const float ideal_step =
        minimum_number(tuning.maximum_angle, tuning.angle_scale / (speed + epsilon));
    const float velocity_yaw = planar_movement_yaw(velocity);
    auto smoothing = runtime.config.air_strafe_smoothing.value;
    if (selection.pre_speed_requested) {
        selection.pre_speed_effective = !convar_self_integer(runtime.pre_speed_guard);
        if (selection.pre_speed_effective) {
            angles.yaw =
                pre_speed_heading(runtime, player, simulation, velocity, simulation.velocity,
                                  history.primary_heading, angles.yaw);
            if (resolve_pre_speed_feedback(history.primary_heading, ideal_step,
                                           tuning.feedback_turn_limit, tuning.side_magnitude,
                                           feedback, command.movement, angles))
                return;
            smoothing = 0;
        } else {
            smoothing = 100;
            apply_guarded_pre_speed_heading(speed, velocity_yaw, history.primary_heading,
                                            tuning.maximum_angle, angles);
        }
    } else if (apply_z_hop_heading(z_hop(runtime), runtime.input, runtime.engine_time, angles)) {
        smoothing = 0;
    }
    const AirSteeringInput steering{velocity_yaw, ideal_step,
                                    speed,        tuning.side_magnitude,
                                    smoothing,    (selection.movement_key_mode & 0xFFu) != 0};
    AirSteeringState state{feedback.side_flip != 0, feedback.primary_heading};
    write_air_strafe_step(steering, state, command.movement, angles);
    feedback.side_flip = static_cast<std::uint8_t>(state.side_flip);
    feedback.primary_heading = state.feedback_heading;
}
}

void synthesize_release_command(MovementReleaseRuntime& runtime, void* player, void* weapon,
                                client::Command& command, MovementSimulation& simulation,
                                MovementFeedback& feedback) {
    const MovementFeedbackHistory history{feedback.primary_heading, feedback.turn_direction,
                                          feedback.secondary_heading};
    feedback.primary_heading = std::numeric_limits<float>::max();
    feedback.turn_direction = 0.0f;
    feedback.secondary_heading = std::numeric_limits<float>::max();
    feedback.original_buttons = command.movement.buttons;
    if (simulation.movement_type == 2 &&
        field<std::uint8_t>(player, runtime.offsets.player_state_ac7f2913) < 2) {
        if (!(simulation.player_flags & on_ground)) {
            if (runtime.config.bunny_hop.value && convar_self_integer(runtime.bunny_hop_guard))
                command.movement.buttons &= ~jump_button;
            if (runtime.force_duck_mode == 1 && !(command.movement.buttons & back_button))
                command.movement.buttons |= duck_button;
        }
        if (runtime.config.air_strafe.value && !(feedback.original_buttons & speed_button)) {
            const bool normalized = runtime.config.config_ad536de0.value != 0;
            const auto selection = select_modes(runtime, normalized, command.movement);
            if (selection.continue_solver) {
                MovementAngleView angles{command.angles};
                if (normalized)
                    synthesize_normalized(runtime, player, weapon, simulation, history, selection,
                                          feedback, command, angles);
                else
                    synthesize_raw(runtime, player, weapon, simulation, history, selection,
                                   feedback, command, angles);
            }
        }
    }
    feedback.was_grounded = static_cast<std::uint8_t>((simulation.player_flags & on_ground) != 0);
}

} // namespace skeet
