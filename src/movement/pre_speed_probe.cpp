#include "skeet/movement/pre_speed_probe.hpp"
#include "skeet/movement/steering_math.hpp"
#include "movement_math.hpp"
#include <limits>

namespace skeet {
namespace {

std::int32_t forecast_tick_count(float horizon, float interval) noexcept {
    const float count = horizon / interval + 0.5f;
    // CVTTSS2SI produces the indefinite integer for NaN and overflow.
    if (!(count >= -2147483648.0f && count < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(count);
}

void trace_pre_speed_hull(const PreSpeedProbeInput& input, MovementCollisionEngine& engine,
                          MovementVector start, MovementVector end, MovementCollisionTrace& trace) {
    engine.trace_ray(make_movement_hull_ray(start, end, input.hull_minimum, input.hull_maximum),
                     input.collision_mask,
                     {MovementCollisionFilterKind::skip_player, input.player, 8, 0}, trace);
}

} // namespace

PreSpeedProbeResult forecast_pre_speed_circle(const PreSpeedProbeInput& input,
                                              MovementCollisionEngine& engine) {
    using namespace movement_detail;
    PreSpeedProbeResult result{false, input.position, input.velocity,
                               (input.player_flags & skeet::on_ground) != 0, 0};
    float yaw = planar_movement_yaw(result.velocity);
    const float gravity_step = input.gravity * input.interval_per_tick;
    const auto count = forecast_tick_count(input.horizon, input.interval_per_tick);
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    for (std::int32_t tick = 0; tick < count; ++tick) {
        if (result.on_ground)
            result.velocity.z = input.jump_impulse;
        else
            result.velocity.z -= gravity_step;

        yaw += input.turn_step;
        const float radians = yaw * degrees_to_radians;
        const float sine = std::sin(radians), cosine = std::cos(radians);
        const float speed = std::sqrt(result.velocity.x * result.velocity.x +
                                      result.velocity.y * result.velocity.y);
        result.velocity.x = speed * cosine;
        result.velocity.y = speed * sine;
        const MovementVector destination{
            input.interval_per_tick * result.velocity.x + result.position.x,
            input.interval_per_tick * result.velocity.y + result.position.y,
            input.interval_per_tick * result.velocity.z + result.position.z};
        trace_pre_speed_hull(input, engine, result.position, destination, trace);
        if ((trace.fraction != 1.0f && trace.plane_normal.z <= 0.9f) || trace.start_solid ||
            trace.all_solid) {
            result.blocked = true;
            return result;
        }

        result.position = trace.end;
        const MovementVector below{result.position.x, result.position.y, result.position.z - 2.0f};
        trace_pre_speed_hull(input, engine, result.position, below, trace);
        result.on_ground = (1.0f > trace.fraction || trace.start_solid || trace.all_solid) &&
                           trace.plane_normal.z >= 0.7f;
        ++result.completed_ticks;
    }
    return result;
}

} // namespace skeet
