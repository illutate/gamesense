#include "skeet/movement/raw_air_solver.hpp"
#include "skeet/movement/air_input.hpp"
#include "skeet/movement/air_feedback.hpp"
#include "skeet/movement/air_steering.hpp"
#include "skeet/movement/pre_speed_controller.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

bool process_raw_air_strafe(const RawAirStrafeSettings& settings,
                            const AirStrafeSelection& selection, InputBitmap input,
                            const RawAirStrafeFrame& frame, MovementFeedback& feedback,
                            client::Command& command, MovementAngleView angles,
                            const AirWallSteeringBindings& bindings) {
    const auto velocity = frame.collision.velocity;
    if (!prepare_air_strafe_heading(selection, velocity, false, command.movement, angles))
        return false;
    auto wall = frame.collision;
    wall.angle_scale = settings.limits.angle_scale;
    wall.maximum_angle = settings.limits.maximum_angle;
    wall.side_magnitude = settings.side_magnitude;
    steer_air_wall(wall, feedback, command, angles, bindings);
    const float speed = horizontal_length(velocity);
    const float ideal_step = minimum_number(settings.limits.maximum_angle,
                                            settings.limits.angle_scale / (speed + epsilon));
    const float velocity_yaw = planar_movement_yaw(velocity);
    auto smoothing = settings.smoothing;
    if (selection.pre_speed_requested) {
        if (!selection.pre_speed_effective) {
            smoothing = 100;
            apply_guarded_pre_speed_heading(speed, velocity_yaw, frame.previous_heading,
                                            settings.limits.maximum_angle, angles);
        } else {
            const auto& snapshot = bindings.snapshot;
            const auto& rules = bindings.rules;
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
            const RawPreSpeedInput pre_speed{settings.limits, forecast, wall.position,
                                             bindings.environment.physics.player_speed_limit,
                                             frame.previous_heading};
            angles.yaw = choose_raw_pre_speed_heading(pre_speed, angles.yaw, bindings.collision,
                                                      bindings.environment.engine_trace_flag);
            if (resolve_pre_speed_feedback(frame.previous_heading, ideal_step,
                                           settings.feedback_turn_limit, settings.side_magnitude,
                                           feedback, command.movement, angles))
                return true;
            smoothing = 0;
        }
    } else if (apply_z_hop_heading(settings.z_hop, input, frame.engine_time, angles))
        smoothing = 0;
    const AirSteeringInput steering{velocity_yaw, ideal_step,
                                    speed,        settings.side_magnitude,
                                    smoothing,    (selection.movement_key_mode & 0xffu) != 0};
    AirSteeringState state{feedback.side_flip != 0, feedback.primary_heading};
    write_air_strafe_step(steering, state, command.movement, angles);
    feedback.side_flip = std::uint8_t(state.side_flip);
    feedback.primary_heading = state.feedback_heading;
    return true;
}

} // namespace skeet
