#include "skeet/movement/pre_speed_controller.hpp"
#include "skeet/movement/steering_math.hpp"
#include "movement_math.hpp"
#include <limits>

namespace skeet {
using namespace movement_detail;
namespace {
struct RadialWall {
    bool found;
    MovementVector normal;
};
RadialWall find_pre_speed_wall(const RawPreSpeedInput& input, MovementCollisionEngine& engine,
                               std::uint8_t* trace_flag) {
    MovementTraceFlagGuard guard(trace_flag);
    float nearest = 1.0f;
    MovementVector normal{};
    MovementCollisionTrace trace{};
    trace.hitgroup = trace.hitbox = -1;
    trace.fraction = 1.0f;
    float radians = 0.0f;
    do {
        const MovementVector destination{input.radial_origin.x + std::cos(radians) * 64.0f,
                                         input.radial_origin.y + std::sin(radians) * 64.0f,
                                         input.radial_origin.z};
        engine.trace_ray(make_movement_hull_ray(input.radial_origin, destination,
                                                input.forecast.hull_minimum,
                                                input.forecast.hull_maximum),
                         1u, {MovementCollisionFilterKind::reject_entities, 0, 0, 0}, trace);
        // Native COMISS/JAE retains a new unordered fraction as well.
        if (!(trace.fraction >= nearest)) {
            nearest = trace.fraction;
            normal = trace.plane_normal;
        }
        radians += 0.31415927410125732421875f;
    } while (radians < 6.283185482025146484375f);
    return {nearest != 1.0f && !(normal.z >= 0.1f), normal};
}
float wall_or_turn(const RawPreSpeedInput& input, MovementCollisionEngine& engine,
                   std::uint8_t* trace_flag, float base, float turn) {
    const auto wall = find_pre_speed_wall(input, engine, trace_flag);
    if (!wall.found)
        return base + turn;
    const float wall_heading = planar_movement_yaw(wall.normal);
    const float raw_difference = wall_heading - base;
    if (std::isnan(raw_difference))
        return wall_heading;
    const float difference = normalize_movement_angle(raw_difference);
    if (difference > 2.5f)
        return base + 2.5f;
    if (-2.5f > difference)
        return base - 2.5f;
    return wall_heading;
}
}

float choose_raw_pre_speed_heading(const RawPreSpeedInput& input, float movement_heading,
                                   MovementCollisionEngine& engine,
                                   std::uint8_t* engine_trace_flag) {
    const float base = input.previous_heading != std::numeric_limits<float>::max()
                           ? input.previous_heading
                           : movement_heading;
    MovementTraceFlagGuard guard(engine_trace_flag);
    const auto prepared = prepare_pre_speed_turn(input.limits, input.forecast.interval_per_tick,
                                                 input.player_speed_limit, input.forecast.velocity);
    float turn = prepared.turn_step;
    if (prepared.within_speed_limit)
        return wall_or_turn(input, engine, engine_trace_flag, base, turn);
    auto trial = input.forecast;
    trial.horizon = sse_min(sse_max(320.0f / prepared.horizontal_speed, 0.35f), 1.0f);
    for (;;) {
        trial.turn_step = turn;
        if (!forecast_pre_speed_circle(trial, engine).blocked)
            break;
        if (prepared.maximum_step <= turn)
            break;
        turn += 0.2f;
    }
    if (!(turn >= prepared.maximum_step))
        return base + (turn + 0.2f);
    turn = prepared.minimum_step;
    for (;;) {
        trial.turn_step = turn;
        if (!forecast_pre_speed_circle(trial, engine).blocked)
            break;
        if (turn <= negate(prepared.minimum_step))
            break;
        turn -= 0.2f;
    }
    if (!(negate(prepared.minimum_step) >= turn))
        return base + (turn - 0.2f);
    return wall_or_turn(input, engine, engine_trace_flag, base, turn);
}

} // namespace skeet
