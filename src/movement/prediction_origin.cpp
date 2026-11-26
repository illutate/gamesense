#include "skeet/movement/prediction.hpp"

namespace skeet {
void predict_movement_origin(MovementVector& origin, MovementVector velocity,
                             MovementVector minimum, MovementVector maximum, std::int32_t ticks,
                             float interval, MovementCollisionEngine& collision,
                             std::uint8_t* trace_flag) {
    const float speed_squared = velocity.x * velocity.x + velocity.y * velocity.y;
    if (!(speed_squared > 25.0f) || !(115600.0f > speed_squared))
        return;
    maximum.z -= 18.0f;
    MovementVector start = origin;
    start.z += 18.0f;
    const float duration = minimum_number(0.2f, static_cast<float>(ticks) * interval);
    const MovementVector end{velocity.x * duration + origin.x, velocity.y * duration + origin.y,
                             duration * 0.0f + start.z};
    auto trace = MovementCollisionTrace{};
    trace.fraction = 1;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    trace_movement_world(collision, make_movement_hull_ray(start, end, minimum, maximum), 1, trace,
                         trace_flag);
    origin.x = trace.end.x;
    origin.y = trace.end.y;
    const auto next = trace.end;
    trace.fraction = 1;
    trace.all_solid = 0;
    trace.start_solid = 0;
    trace.entity = 0;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    trace_movement_world(
        collision, make_movement_hull_ray(next, {next.x, next.y, next.z - 18.0f}, minimum, maximum),
        1, trace, trace_flag);
    origin.z = trace.end.z;
}
} // namespace skeet
