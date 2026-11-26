#include "skeet/movement/air_wall_steering.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

std::int32_t truncate_tick_count(float value) noexcept {
    // CVTTSS2SI returns the integer-indefinite value outside the signed range.
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

MovementCollisionTrace initial_trace() noexcept {
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    return trace;
}

bool surface_blocks_steering(void* surface_properties, std::int16_t property) {
    const auto* surface =
        engine::invoke<const std::uint8_t*>(surface_properties, 5, std::int32_t(property));
    return surface && surface[0x62] != 0;
}

void write_wall_heading(MovementVector direction, MovementVector forward,
                        const MovementCollisionTrace& trace,
                        MovementAngleView movement_angles) noexcept {
    using namespace movement_detail;
    const MovementVector basis =
        !(dot(direction, trace.plane_normal) >= -0.8660252690315247f) ? forward : direction;
    MovementVector tangent;
    if (trace.plane_normal.x == 0.0f && trace.plane_normal.y == 0.0f) {
        tangent = {0.0f, -1.0f, 0.0f};
    } else {
        tangent = {trace.plane_normal.y - trace.plane_normal.z * 0.0f,
                   trace.plane_normal.z * 0.0f - trace.plane_normal.x,
                   trace.plane_normal.x * 0.0f - trace.plane_normal.y * 0.0f};
        const float reciprocal =
            1.0f /
            (std::sqrt((tangent.x * tangent.x + tangent.y * tangent.y) + tangent.z * tangent.z) +
             epsilon);
        tangent = {tangent.x * reciprocal, tangent.y * reciprocal, tangent.z * reciprocal};
    }
    if (!(dot(tangent, basis) >= 0.0f)) {
        tangent.x = negate(tangent.x);
        tangent.y = negate(tangent.y);
    }
    const float weight = 1.0f - trace.fraction;
    const MovementVector blended{(tangent.x - forward.x) * weight + forward.x,
                                 (tangent.y - forward.y) * weight + forward.y, 0.0f};
    movement_angles.yaw = planar_movement_yaw(blended);
}
}

bool steer_air_wall(const AirWallSteeringInput& input, const MovementFeedback& feedback,
                    const client::Command& command, MovementAngleView movement_angles,
                    const AirWallSteeringBindings& bindings) {
    using namespace movement_detail;
    if (feedback.prediction_blocked || !input.enabled ||
        (feedback.original_buttons & 0x20026u) != 2u)
        return false;

    const float squared =
        (input.velocity.y * input.velocity.y + 0.0f) + input.velocity.x * input.velocity.x;
    const float inverse = 1.0f / (std::sqrt(squared) + epsilon);
    const MovementVector direction{input.velocity.x * inverse, input.velocity.y * inverse,
                                   0.0f * inverse};
    const float radians = movement_angles.yaw * degrees_to_radians;
    const MovementVector forward{std::cos(radians), std::sin(radians), 0.0f};
    // COMISS/JB rejects both a negative dot and an unordered comparison.
    if (!(dot(forward, direction) >= 0.0f))
        return false;

    const float seconds = static_cast<float>(input.prediction_milliseconds) * 0.001f;
    const auto ticks = truncate_tick_count(seconds / bindings.rules.interval_per_tick + 0.5f);
    MovementSimulation simulation{};
    initialize_movement_simulation(simulation, input.player, input.position, input.velocity,
                                   input.player_flags, 0, bindings.snapshot, bindings.rules);
    auto simulated_command = command;
    simulated_command.angles = {movement_angles.pitch, movement_angles.yaw, movement_angles.roll};
    const float turn = minimum_number(
        input.maximum_angle, input.angle_scale / (horizontal_length(input.velocity) + epsilon));
    if (input.weapon_handle != -1)
        engine::invoke<std::uint32_t>(bindings.entity_list, 4, std::uint32_t(input.weapon_handle));
    bool side_flip = feedback.side_flip != 0;
    for (std::int32_t tick = 0; tick < ticks; ++tick) {
        simulated_command.angles[1] =
            side_flip ? movement_angles.yaw - turn : movement_angles.yaw + turn;
        simulated_command.movement.side_move =
            side_flip ? negate(input.side_magnitude) : input.side_magnitude;
        side_flip = !side_flip;
        simulate_movement_tick(simulation, bindings.collision, bindings.environment, input.player,
                               simulated_command.movement,
                               {simulated_command.angles[0], simulated_command.angles[1],
                                simulated_command.angles[2]});
        if (simulation.simulation_flags & 2u)
            break;
        simulated_command.number =
            std::bit_cast<std::int32_t>(std::uint32_t(simulated_command.number) + 1u);
    }
    if (!(simulation.simulation_flags & 2u))
        return true;

    MovementTraceFlagGuard trace_flag(bindings.environment.engine_trace_flag);
    auto trace = initial_trace();
    const MovementVector start{input.position.x, input.position.y, input.position.z + 18.0f};
    const MovementVector extended{forward.x * 320.0f, forward.y * 320.0f, forward.z * 320.0f};
    const MovementVector maximum{bindings.snapshot.hull_maximum.x, bindings.snapshot.hull_maximum.y,
                                 24.0f};
    const MovementCollisionFilter filter{MovementCollisionFilterKind::reject_entities, 0, 0, 0};
    auto ray = make_movement_hull_ray(
        start,
        {extended.x * 0.5f + start.x, extended.y * 0.5f + start.y, extended.z * 0.5f + start.z},
        bindings.snapshot.hull_minimum, maximum);
    bindings.collision.trace_ray(ray, 0x200400bu, filter, trace);
    if (trace.plane_normal.z != 1.0f &&
        ((trace.contents & 0x20000000u) ||
         surface_blocks_steering(bindings.surface_properties, trace.surface_property)))
        return true;

    ray = make_movement_hull_ray(start,
                                 {extended.x * seconds + start.x, extended.y * seconds + start.y,
                                  extended.z * seconds + start.z},
                                 bindings.snapshot.hull_minimum, maximum);
    bindings.collision.trace_ray(ray, bindings.snapshot.collision_mask & 0xfffeffffu, filter,
                                 trace);
    if (!(trace.fraction < 1.0f || trace.all_solid || trace.start_solid) ||
        !(absolute(trace.plane_normal.z) < 0.1f) ||
        ballistic_breakable(bindings.ballistics, trace.entity))
        return true;

    write_wall_heading(direction, forward, trace, movement_angles);
    return true;
}

bool steer_live_air_wall(const AirWallSteeringInput& input, const MovementFeedback& feedback,
                         const client::Command& command, MovementAngleView movement_angles,
                         const AirWallSteeringLiveBindings& bindings) {
    using namespace movement_detail;
    if (feedback.prediction_blocked || !input.enabled ||
        (feedback.original_buttons & 0x20026u) != 2u)
        return false;

    const float squared =
        (input.velocity.y * input.velocity.y + 0.0f) + input.velocity.x * input.velocity.x;
    const float inverse = 1.0f / (std::sqrt(squared) + epsilon);
    const MovementVector direction{input.velocity.x * inverse, input.velocity.y * inverse,
                                   0.0f * inverse};
    const float radians = movement_angles.yaw * degrees_to_radians;
    const MovementVector forward{std::cos(radians), std::sin(radians), 0.0f};
    if (!(dot(forward, direction) >= 0.0f))
        return false;

    // Native 43379DB8 and 43379DFA precede the constructor's own hull reads.
    // The minimum is retained as a pointer and dereferenced at each trace;
    // mutations made by subsequent engine getters/traces remain observable.
    const auto* minimum = engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    const MovementVector trace_position = bindings.origin;
    const MovementVector start{trace_position.x, trace_position.y, trace_position.z + 18.0f};
    const MovementVector maximum_snapshot =
        *engine::invoke<const MovementVector*>(bindings.player, 0x134u / 4u);
    const MovementVector maximum{maximum_snapshot.x, maximum_snapshot.y, 24.0f};
    const MovementVector extended{forward.x * 320.0f, forward.y * 320.0f, forward.z * 320.0f};
    const float seconds = static_cast<float>(bindings.tuning.prediction_milliseconds) * 0.001f;
    const auto flags =
        field<std::uint32_t>(bindings.player, bindings.environment.fields.player_flags_e2fbe695);
    const MovementVector constructor_velocity = bindings.velocity;
    const MovementVector constructor_position = bindings.origin;
    MovementSimulation simulation{};
    initialize_live_movement_simulation(simulation, bindings.player, constructor_position,
                                        constructor_velocity, flags, 0, bindings.capture);
    const auto ticks = truncate_tick_count(seconds / (*bindings.environment.interval) + 0.5f);
    auto simulated_command = command;
    simulated_command.angles = {movement_angles.pitch, movement_angles.yaw, movement_angles.roll};
    const float turn =
        minimum_number(bindings.tuning.maximum_angle,
                       bindings.tuning.angle_scale / (horizontal_length(input.velocity) + epsilon));
    const auto weapon_handle =
        field<std::int32_t>(bindings.player, bindings.environment.fields.weapon_handle_cd812654);
    if (weapon_handle != -1)
        engine::invoke<std::uint32_t>(bindings.environment.entity_list, 4,
                                      std::uint32_t(weapon_handle));
    bool side_flip = feedback.side_flip != 0;
    for (std::int32_t tick = 0; tick < ticks; ++tick) {
        simulated_command.angles[1] =
            side_flip ? movement_angles.yaw - turn : movement_angles.yaw + turn;
        simulated_command.movement.side_move =
            side_flip ? negate(bindings.tuning.side_magnitude) : bindings.tuning.side_magnitude;
        side_flip = !side_flip;
        simulate_live_movement_tick(simulation, bindings.collision, bindings.environment,
                                    bindings.player, simulated_command);
        if (simulation.simulation_flags & 2u)
            break;
        simulated_command.number =
            std::bit_cast<std::int32_t>(std::uint32_t(simulated_command.number) + 1u);
    }
    if (!(simulation.simulation_flags & 2u))
        return true;

    MovementTraceFlagGuard trace_flag(bindings.environment.engine_trace_flag);
    auto trace = initial_trace();
    const MovementCollisionFilter filter{MovementCollisionFilterKind::reject_entities, 0, 0, 0};
    bindings.collision.trace_ray(
        make_movement_hull_ray(
            start,
            {extended.x * 0.5f + start.x, extended.y * 0.5f + start.y, extended.z * 0.5f + start.z},
            *minimum, maximum),
        0x200400bu, filter, trace);
    if (trace.plane_normal.z != 1.0f &&
        ((trace.contents & 0x20000000u) ||
         surface_blocks_steering(bindings.surface_properties, trace.surface_property)))
        return true;

    const auto mask = engine::invoke<std::uint32_t>(bindings.player, 0x260u / 4u) & 0xfffeffffu;
    bindings.collision.trace_ray(
        make_movement_hull_ray(start,
                               {extended.x * seconds + start.x, extended.y * seconds + start.y,
                                extended.z * seconds + start.z},
                               *minimum, maximum),
        mask, filter, trace);
    if (!(trace.fraction < 1.0f || trace.all_solid || trace.start_solid) ||
        !(absolute(trace.plane_normal.z) < 0.1f) ||
        ballistic_breakable(bindings.ballistics, trace.entity))
        return true;

    write_wall_heading(direction, forward, trace, movement_angles);
    return true;
}

} // namespace skeet
