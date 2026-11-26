#include "skeet/movement/normalized_air_solver.hpp"
#include "skeet/movement/air_input.hpp"
#include "skeet/movement/air_feedback.hpp"
#include "skeet/movement/pre_speed_controller.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

bool process_normalized_air_strafe(const NormalizedAirStrafeSettings& settings,
                                   const AirStrafeSelection& selection, InputBitmap input,
                                   const NormalizedAirStrafeFrame& frame,
                                   const MovementFeedbackHistory& history,
                                   MovementFeedback& feedback, client::Command& command,
                                   MovementAngleView angles,
                                   const NormalizedAirStrafeBindings& bindings) {
    const auto& steering = settings.steering;
    const auto velocity = frame.collision.velocity;
    if (!prepare_air_strafe_heading(selection, velocity, true, command.movement, angles))
        return false;
    auto wall = frame.collision;
    wall.maximum_angle = steering.limits.maximum_angle;
    wall.side_magnitude = steering.side_magnitude;
    wall.smoothing = steering.smoothing;
    wall.acceleration_limit = steering.feedback_turn_limit;
    wall.angle_step = settings.angle_step;
    wall.pre_speed_effective = selection.pre_speed_effective;
    const float velocity_yaw =
        steer_normalized_air_wall(wall, feedback, command, angles, bindings.wall);
    const float speed = horizontal_length(velocity);
    if (selection.pre_speed_requested) {
        if (selection.pre_speed_effective) {
            const auto& snapshot = bindings.wall.snapshot;
            const auto& rules = bindings.wall.rules;
            const PreSpeedProbeInput forecast{frame.forecast_position,
                                              velocity,
                                              frame.forecast_player_flags,
                                              rules.gravity,
                                              rules.jump_impulse,
                                              rules.interval_per_tick,
                                              0.0f,
                                              0.0f,
                                              snapshot.hull_minimum,
                                              snapshot.hull_maximum,
                                              snapshot.collision_mask,
                                              wall.player};
            const RawPreSpeedInput pre_speed{steering.limits, forecast, wall.position,
                                             bindings.wall.environment.physics.player_speed_limit,
                                             history.primary_heading};
            angles.yaw =
                choose_raw_pre_speed_heading(pre_speed, angles.yaw, bindings.wall.collision,
                                             bindings.wall.environment.engine_trace_flag);
            const float ideal_step = minimum_number(
                steering.limits.maximum_angle, steering.limits.angle_scale / (speed + epsilon));
            if (resolve_pre_speed_feedback(history.primary_heading, ideal_step,
                                           steering.feedback_turn_limit, steering.side_magnitude,
                                           feedback, command.movement, angles))
                return true;
        }
    } else
        apply_z_hop_heading(steering.z_hop, input, frame.engine_time, angles);
    const NormalizedAirFinalSettings final_settings{
        steering.smoothing, steering.limits.maximum_angle, steering.side_magnitude,
        steering.feedback_turn_limit, settings.angle_step};
    const NormalizedAirFinalInput final_input{wall.position, velocity, frame.stamina,
                                              velocity_yaw,  speed,    selection};
    finish_normalized_air_strafe(final_settings, final_input, history, feedback, command, angles,
                                 bindings.final);
    return true;
}

} // namespace skeet
