#include "skeet/movement/collision.hpp"
#include "movement_math.hpp"

namespace skeet {

MovementTraceFlagGuard::MovementTraceFlagGuard(std::uint8_t* flag) noexcept : flag_(flag) {
    if (flag_) {
        saved_ = *flag_;
        *flag_ = 0;
    }
}

MovementTraceFlagGuard::~MovementTraceFlagGuard() {
    restore();
}

void MovementTraceFlagGuard::restore() noexcept {
    if (flag_) {
        *flag_ = saved_;
        flag_ = nullptr;
    }
}

void trace_movement_world(MovementCollisionEngine& engine, const MovementCollisionRay& ray,
                          std::uint32_t mask, MovementCollisionTrace& trace,
                          std::uint8_t* engine_trace_flag) {
    MovementTraceFlagGuard guard(engine_trace_flag);
    engine.trace_ray(ray, mask, {MovementCollisionFilterKind::reject_entities, 0, 0, 0}, trace);
}

MovementCollisionRay make_movement_hull_ray(MovementVector start, MovementVector end,
                                            MovementVector minimum,
                                            MovementVector maximum) noexcept {
    MovementCollisionRay ray;
    ray.delta = {end.x - start.x, end.y - start.y, end.z - start.z};
    ray.swept =
        (ray.delta.y * ray.delta.y + ray.delta.x * ray.delta.x) + ray.delta.z * ray.delta.z != 0.0f;
    ray.extents = {(maximum.x - minimum.x) * 0.5f, (maximum.y - minimum.y) * 0.5f,
                   (maximum.z - minimum.z) * 0.5f};
    const float squared = (ray.extents.y * ray.extents.y + ray.extents.x * ray.extents.x) +
                          ray.extents.z * ray.extents.z;
    ray.is_ray = static_cast<double>(squared) < 1.0e-6;
    const MovementVector center{(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
                                (minimum.z + maximum.z) * 0.5f};
    ray.start = {start.x + center.x, start.y + center.y, start.z + center.z};
    ray.start_offset = {center.x * -1.0f, center.y * -1.0f, center.z * -1.0f};
    return ray;
}

} // namespace skeet
