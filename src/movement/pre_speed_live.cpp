#include "skeet/movement/pre_speed_live.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/engine/interface_call.hpp"
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
float convar_float(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<float>(
        field<std::uint32_t>(object, 0x2Cu) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
std::int32_t tick_count(float value) noexcept {
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
void trace_forecast(const LivePreSpeedBindings& bindings, MovementVector start, MovementVector end,
                    MovementCollisionTrace& trace) {
    const auto mask = engine::invoke<std::uint32_t>(bindings.player, 0x260u / 4u);
    const auto* maximum = engine::invoke<const MovementVector*>(bindings.player, 0x134u / 4u);
    const auto* minimum = engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    bindings.collision.trace_ray(
        make_movement_hull_ray(start, end, *minimum, *maximum), mask,
        {MovementCollisionFilterKind::skip_player,
         static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bindings.player)), 8, 0},
        trace);
}
bool forecast_circle(const LivePreSpeedInput& input, float turn, float horizon,
                     const LivePreSpeedBindings& bindings) {
    auto velocity = input.forecast_velocity;
    float yaw = planar_movement_yaw(velocity);
    const float gravity_step =
        convar_float(bindings.capture.convars.gravity) * (*bindings.environment.interval);
    const float jump_impulse = convar_float(bindings.capture.convars.jump_impulse);
    const auto count = tick_count(horizon / (*bindings.environment.interval) + 0.5f);
    bool ground =
        (field<std::uint8_t>(bindings.player, bindings.environment.fields.player_flags_e2fbe695) &
         on_ground) != 0;
    auto position = field<MovementVector>(bindings.player, bindings.forecast_position_a9ee6062);
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    for (std::int32_t tick = 0; tick < count; ++tick) {
        if (ground)
            velocity.z = jump_impulse;
        else
            velocity.z -= gravity_step;
        yaw += turn;
        const float radians = yaw * degrees_to_radians;
        const float sine = std::sin(radians), cosine = std::cos(radians);
        const float speed = horizontal_length(velocity);
        velocity.x = speed * cosine;
        velocity.y = speed * sine;
        const float interval = *bindings.environment.interval;
        const MovementVector destination{interval * velocity.x + position.x,
                                         interval * velocity.y + position.y,
                                         interval * velocity.z + position.z};
        trace_forecast(bindings, position, destination, trace);
        if ((trace.fraction != 1.0f && trace.plane_normal.z <= 0.9f) || trace.start_solid ||
            trace.all_solid)
            return true;
        position = trace.end;
        trace_forecast(bindings, position, {position.x, position.y, position.z - 2.0f}, trace);
        ground = (1.0f > trace.fraction || trace.start_solid || trace.all_solid) &&
                 trace.plane_normal.z >= 0.7f;
    }
    return false;
}
float radial_heading(const LivePreSpeedInput& input, float base, float turn,
                     const LivePreSpeedBindings& bindings) {
    MovementTraceFlagGuard guard(bindings.environment.engine_trace_flag);
    const auto* minimum = engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    const auto* maximum = engine::invoke<const MovementVector*>(bindings.player, 0x134u / 4u);
    float nearest = 1.0f;
    MovementVector normal{};
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    float radians = 0.0f;
    do {
        const float dx = std::cos(radians) * 64.0f;
        const float z = input.radial_origin.z;
        const float dy = std::sin(radians) * 64.0f;
        const auto origin = input.radial_origin;
        const MovementVector end{origin.x + dx, origin.y + dy, z};
        bindings.collision.trace_ray(make_movement_hull_ray(origin, end, *minimum, *maximum), 1u,
                                     {MovementCollisionFilterKind::reject_entities, 0, 0, 0},
                                     trace);
        if (!(trace.fraction >= nearest)) {
            nearest = trace.fraction;
            normal = trace.plane_normal;
        }
        radians += 0.31415927410125732421875f;
    } while (radians < 6.283185482025146484375f);
    if (nearest == 1.0f || normal.z >= 0.1f)
        return base + turn;
    const float wall = planar_movement_yaw(normal);
    const float raw = wall - base;
    if (std::isnan(raw))
        return wall;
    const float difference = normalize_movement_angle(raw);
    if (difference > 2.5f)
        return base + 2.5f;
    if (-2.5f > difference)
        return base - 2.5f;
    return wall;
}
}

float choose_live_pre_speed_heading(const LivePreSpeedInput& input, float movement_heading,
                                    const LivePreSpeedBindings& bindings) {
    using namespace movement_detail;
    const float base = input.previous_heading != std::numeric_limits<float>::max()
                           ? input.previous_heading
                           : movement_heading;
    MovementTraceFlagGuard guard(bindings.environment.engine_trace_flag);
    const auto prepared = prepare_pre_speed_turn(
        input.limits, *bindings.environment.interval,
        field<float>(bindings.player, bindings.environment.fields.speed_limit_3eb71843),
        input.velocity);
    float turn = prepared.turn_step;
    if (prepared.within_speed_limit)
        return radial_heading(input, base, turn, bindings);
    const float horizon = sse_min(sse_max(320.0f / prepared.horizontal_speed, 0.35f), 1.0f);
    for (;;) {
        if (!forecast_circle(input, turn, horizon, bindings))
            break;
        if (prepared.maximum_step <= turn)
            break;
        turn += 0.2f;
    }
    if (!(turn >= prepared.maximum_step))
        return base + (turn + 0.2f);
    turn = prepared.minimum_step;
    for (;;) {
        if (!forecast_circle(input, turn, horizon, bindings))
            break;
        if (turn <= negate(prepared.minimum_step))
            break;
        turn -= 0.2f;
    }
    if (!(negate(prepared.minimum_step) >= turn))
        return base + (turn - 0.2f);
    return radial_heading(input, base, turn, bindings);
}

} // namespace skeet
