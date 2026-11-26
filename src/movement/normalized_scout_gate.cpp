#include "skeet/movement/normalized_scout_gate.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
using namespace movement_detail;

MovementCollisionRay scout_hull_ray(MovementVector origin, MovementVector delta,
                                    float horizontal_delta_squared, const MovementVector& minimum,
                                    const MovementVector& maximum, bool elevated) noexcept {
    MovementCollisionRay ray;
    ray.delta = delta;
    ray.swept = delta.z * delta.z + horizontal_delta_squared != 0.0f;
    ray.extents = {(maximum.x - minimum.x) * 0.5f, (maximum.y - minimum.y) * 0.5f,
                   (maximum.z - minimum.z) * 0.5f};
    const float squared = (ray.extents.y * ray.extents.y + ray.extents.x * ray.extents.x) +
                          ray.extents.z * ray.extents.z;
    ray.is_ray = static_cast<double>(squared) < 1.0e-6;
    const MovementVector center{(maximum.x + minimum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
                                (minimum.z + maximum.z) * 0.5f};
    ray.start = {center.x + origin.x, origin.y + center.y,
                 elevated ? center.z + origin.z : origin.z + center.z};
    ray.start_offset = {center.x * -1.0f, center.y * -1.0f, center.z * -1.0f};
    return ray;
}

bool blocked(const MovementCollisionTrace& trace) noexcept {
    return 1.0f > trace.fraction || trace.all_solid != 0 || trace.start_solid != 0;
}
}

bool pass_normalized_scout_gate(const NormalizedScoutGateFrame& frame,
                                const client::Command& command, MovementCollisionEngine& collision,
                                std::uint8_t* engine_trace_flag) {
    if (!frame.has_weapon || frame.weapon_definition != 0x28u)
        return true;
    const auto& velocity = frame.original_velocity;
    if (!(4.0f > velocity.y * velocity.y + velocity.x * velocity.x))
        return true;
    const auto& movement = command.movement;
    if (movement.buttons & 0x618u)
        return true;
    if (!(4.0f >
          movement.forward_move * movement.forward_move + movement.side_move * movement.side_move))
        return true;

    auto* collideable = engine::invoke<void*>(frame.player, 3);
    const auto* maximum = engine::invoke<const MovementVector*>(collideable, 2);
    collideable = engine::invoke<void*>(frame.player, 3);
    const auto* minimum = engine::invoke<const MovementVector*>(collideable, 1);
    MovementAngles view;
    engine::invoke<void>(frame.engine_client, 18, &view);

    const float yaw = view.yaw * degrees_to_radians;
    const float sine_yaw = std::sin(yaw);
    const float cosine_yaw = std::cos(yaw);
    const float cosine_pitch = std::cos(view.pitch * degrees_to_radians);
    const float x = cosine_pitch * cosine_yaw;
    const float y = cosine_pitch * sine_yaw;
    const float reciprocal = 1.0f / (std::sqrt((x * x + y * y) + 0.0f) + epsilon);
    const auto& origin = frame.origin;
    const float end_z = (reciprocal * 0.0f) * 64.0f + origin.z;
    MovementVector delta{((reciprocal * x) * 64.0f + origin.x) - origin.x,
                         ((reciprocal * y) * 64.0f + origin.y) - origin.y, end_z - origin.z};
    const float horizontal_delta_squared = delta.y * delta.y + delta.x * delta.x;
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    auto ray = scout_hull_ray(origin, delta, horizontal_delta_squared, *minimum, *maximum, false);
    trace_movement_world(collision, ray, 0x1400bu, trace, engine_trace_flag);
    if (!blocked(trace))
        return true;

    MovementVector elevated_origin{origin.x, origin.y, origin.z + 64.0f};
    delta.z = (end_z + 64.0f) - elevated_origin.z;
    ray =
        scout_hull_ray(elevated_origin, delta, horizontal_delta_squared, *minimum, *maximum, true);
    trace_movement_world(collision, ray, 0x1400bu, trace, engine_trace_flag);
    return !blocked(trace);
}

} // namespace skeet
