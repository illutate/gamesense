#include "skeet/movement/normalized_air_wall.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cmath>
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

float choose_prediction_heading(float velocity_yaw, float step, float previous_yaw) noexcept {
    const float negative = velocity_yaw - step;
    const float positive = velocity_yaw + step;
    const float negative_difference = absolute(normalize_movement_angle(negative - previous_yaw));
    const float positive_difference = absolute(normalize_movement_angle(positive - previous_yaw));
    // COMISS/JBE selects the positive candidate on ties and unordered values.
    return positive_difference > negative_difference ? negative : positive;
}

float live_prediction_yaw(const MovementSimulation& simulation, const client::Command& command,
                          const AirWallSteeringLiveBindings& bindings,
                          const void* const& air_acceleration) {
    const float velocity_yaw = planar_movement_yaw(simulation.velocity);
    const float previous_yaw = command.angles[1];
    const float difference = normalize_movement_angle(previous_yaw - velocity_yaw);
    const float speed = horizontal_length(simulation.velocity);
    const float smoothing =
        sse_min(sse_max(1.0f - static_cast<float>(bindings.tuning.smoothing) * 0.01f, 0.0f), 1.0f);
    // Stamina is saved before the live virtual450 call in the native block.
    const float stamina = simulation.stamina;
    const float player_maximum = engine::invoke<float>(bindings.player, 0x450u / 4u);
    const auto& fields = bindings.environment.fields;
    float maximum =
        minimum_number(field<float>(bindings.player, fields.speed_limit_3eb71843), player_maximum);
    if (stamina > 0.0f) {
        const float scale = sse_min(sse_max(1.0f - stamina / 100.0f, 0.0f), 1.0f);
        maximum *= scale * scale;
    }
    if ((command.movement.buttons & duck_button) ||
        field<std::uint8_t>(bindings.player, fields.ducking_6b1a59b5) ||
        (field<std::uint8_t>(bindings.player, fields.player_flags_e2fbe695) & 2u)) {
        const float amount = field<float>(bindings.player, fields.duck_amount_3309c0cf);
        maximum *=
            ((amount * 0.34f + 1.0f) - field<float>(bindings.player, fields.duck_amount_3309c0cf));
    }
    const float first_friction = simulation.surface_friction;
    const float first_basis = maximum_number(bindings.tuning.acceleration_limit, speed);
    const float first_acceleration =
        ((convar_float(air_acceleration) * maximum) * *bindings.environment.interval) *
        first_friction;
    const float cosine = sse_min(
        sse_max((bindings.tuning.acceleration_limit - first_acceleration) / first_basis, -1.0f),
        1.0f);
    const float minimum =
        minimum_number(std::acos(cosine) * radians_to_degrees, bindings.tuning.maximum_angle);
    const float second_friction = simulation.surface_friction;
    const float second_basis = maximum_number(bindings.tuning.acceleration_limit, speed);
    const float second_acceleration =
        ((convar_float(air_acceleration) * maximum) * *bindings.environment.interval) *
        second_friction;
    const float alternate =
        sse_min(sse_max(negate(minimum_number(bindings.tuning.angle_step, second_acceleration) /
                               (second_basis * 2.0f)),
                        -1.0f),
                1.0f);
    const float target = std::acos(alternate) * radians_to_degrees;
    const float step =
        absolute(difference) > 10.0f ? (target - minimum) * smoothing + minimum : minimum;
    // The getter may mutate the copied command. The initial difference above
    // is already saved, while the nearest-candidate comparisons read it anew.
    return choose_prediction_heading(velocity_yaw, step, command.angles[1]);
}

std::int32_t truncate_tick_count(float value) noexcept {
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
    const MovementVector basis =
        !(dot(direction, trace.plane_normal) >= -0.8660252690315247f) ? forward : direction;
    MovementVector tangent;
    if (trace.plane_normal.x == 0.0f && trace.plane_normal.y == 0.0f) {
        tangent = {0.0f, -1.0f, 0.0f};
    } else {
        tangent = {trace.plane_normal.y - trace.plane_normal.z * 0.0f,
                   trace.plane_normal.z * 0.0f - trace.plane_normal.x,
                   trace.plane_normal.x * 0.0f - trace.plane_normal.y * 0.0f};
        const float inverse =
            1.0f /
            (std::sqrt((tangent.x * tangent.x + tangent.y * tangent.y) + tangent.z * tangent.z) +
             epsilon);
        tangent = {tangent.x * inverse, tangent.y * inverse, tangent.z * inverse};
    }
    if (!(dot(tangent, basis) >= 0.0f)) {
        tangent.x = negate(tangent.x);
        tangent.y = negate(tangent.y);
    }
    const float weight = 1.0f - trace.fraction;
    movement_angles.yaw = planar_movement_yaw({(tangent.x - forward.x) * weight + forward.x,
                                               (tangent.y - forward.y) * weight + forward.y, 0.0f});
}
}

float normalized_air_prediction_yaw(const NormalizedAirWallInput& input,
                                    const MovementSimulation& simulation,
                                    const UserCommand& command, float previous_yaw,
                                    const MovementPhysics& physics) noexcept {
    using namespace movement_detail;
    const float velocity_yaw = planar_movement_yaw(simulation.velocity);
    const float difference = normalize_movement_angle(previous_yaw - velocity_yaw);
    const float speed = horizontal_length(simulation.velocity);
    const float smoothing =
        sse_min(sse_max(1.0f - static_cast<float>(input.smoothing) * 0.01f, 0.0f), 1.0f);
    float maximum = minimum_number(physics.player_speed_limit, physics.player_max_speed);
    if (simulation.stamina > 0.0f) {
        const float scale = sse_min(sse_max(1.0f - simulation.stamina / 100.0f, 0.0f), 1.0f);
        maximum *= scale * scale;
    }
    if ((command.buttons & duck_button) || physics.ducking || (physics.player_flags & 2u))
        maximum *= ((physics.duck_amount * 0.34f + 1.0f) - physics.duck_amount);
    const float basis = maximum_number(input.acceleration_limit, speed);
    const float acceleration =
        ((input.air_acceleration * maximum) * simulation.rules.interval_per_tick) *
        simulation.surface_friction;
    const float cosine =
        sse_min(sse_max((input.acceleration_limit - acceleration) / basis, -1.0f), 1.0f);
    const float minimum =
        minimum_number(std::acos(cosine) * radians_to_degrees, input.maximum_angle);
    const float alternate = sse_min(
        sse_max(negate(minimum_number(input.angle_step, acceleration) / (basis * 2.0f)), -1.0f),
        1.0f);
    const float target = std::acos(alternate) * radians_to_degrees;
    const float step =
        absolute(difference) > 10.0f ? (target - minimum) * smoothing + minimum : minimum;
    return choose_prediction_heading(velocity_yaw, step, previous_yaw);
}

float steer_normalized_air_wall(const NormalizedAirWallInput& input,
                                const MovementFeedback& feedback, const client::Command& command,
                                MovementAngleView movement_angles,
                                const AirWallSteeringBindings& bindings) {
    using namespace movement_detail;
    const float velocity_yaw = planar_movement_yaw(input.velocity);
    if (input.pre_speed_effective || feedback.prediction_blocked || !input.enabled ||
        (feedback.original_buttons & 0x20026u) != 2u)
        return velocity_yaw;
    const float squared =
        (input.velocity.y * input.velocity.y + 0.0f) + input.velocity.x * input.velocity.x;
    const float reciprocal = 1.0f / (std::sqrt(squared) + epsilon);
    const MovementVector direction{input.velocity.x * reciprocal, input.velocity.y * reciprocal,
                                   0.0f * reciprocal};
    const float radians = movement_angles.yaw * degrees_to_radians;
    const MovementVector forward{std::cos(radians), std::sin(radians), 0.0f};
    if (!(dot(forward, direction) >= 0.0f))
        return velocity_yaw;
    const float seconds = static_cast<float>(input.prediction_milliseconds) * 0.001f;
    const auto ticks = truncate_tick_count(seconds / bindings.rules.interval_per_tick + 0.5f);
    MovementSimulation simulation{};
    initialize_movement_simulation(simulation, input.player, input.position, input.velocity,
                                   input.player_flags, 0, bindings.snapshot, bindings.rules);
    auto simulated_command = command;
    simulated_command.angles = {movement_angles.pitch, movement_angles.yaw, movement_angles.roll};
    if (input.weapon_handle != -1)
        engine::invoke<std::uint32_t>(bindings.entity_list, 4, std::uint32_t(input.weapon_handle));
    for (std::int32_t tick = 0; tick < ticks; ++tick) {
        simulated_command.angles[1] = normalized_air_prediction_yaw(
            input, simulation, simulated_command.movement, simulated_command.angles[1],
            bindings.environment.physics);
        simulated_command.movement.side_move = input.side_magnitude;
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
        return velocity_yaw;
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
        return velocity_yaw;
    ray = make_movement_hull_ray(start,
                                 {extended.x * seconds + start.x, extended.y * seconds + start.y,
                                  extended.z * seconds + start.z},
                                 bindings.snapshot.hull_minimum, maximum);
    bindings.collision.trace_ray(ray, bindings.snapshot.collision_mask & 0xfffeffffu, filter,
                                 trace);
    if (!(trace.fraction < 1.0f || trace.all_solid || trace.start_solid) ||
        !(absolute(trace.plane_normal.z) < 0.1f) ||
        ballistic_breakable(bindings.ballistics, trace.entity))
        return velocity_yaw;
    write_wall_heading(direction, forward, trace, movement_angles);
    return velocity_yaw;
}

float steer_live_normalized_air_wall(const NormalizedAirWallInput& input,
                                     const MovementFeedback& feedback,
                                     const client::Command& command,
                                     MovementAngleView movement_angles,
                                     const AirWallSteeringLiveBindings& bindings,
                                     const void* const& air_acceleration) {
    using namespace movement_detail;
    const float velocity_yaw = planar_movement_yaw(input.velocity);
    if (input.pre_speed_effective || feedback.prediction_blocked || !input.enabled ||
        (feedback.original_buttons & 0x20026u) != 2u)
        return velocity_yaw;
    const float squared =
        (input.velocity.y * input.velocity.y + 0.0f) + input.velocity.x * input.velocity.x;
    const float reciprocal = 1.0f / (std::sqrt(squared) + epsilon);
    const MovementVector direction{input.velocity.x * reciprocal, input.velocity.y * reciprocal,
                                   0.0f * reciprocal};
    const float radians = movement_angles.yaw * degrees_to_radians;
    const MovementVector forward{std::cos(radians), std::sin(radians), 0.0f};
    if (!(dot(forward, direction) >= 0.0f))
        return velocity_yaw;

    // These getters precede the constructor's own 130/134/260 captures. The
    // first minimum pointer stays live through both traces; maximum is copied.
    const auto* minimum = engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    const MovementVector trace_position = bindings.origin;
    const MovementVector maximum_snapshot =
        *engine::invoke<const MovementVector*>(bindings.player, 0x134u / 4u);
    const MovementVector start{trace_position.x, trace_position.y, trace_position.z + 18.0f};
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
    const auto weapon_handle =
        field<std::int32_t>(bindings.player, bindings.environment.fields.weapon_handle_cd812654);
    if (weapon_handle != -1)
        engine::invoke<std::uint32_t>(bindings.environment.entity_list, 4,
                                      std::uint32_t(weapon_handle));
    for (std::int32_t tick = 0; tick < ticks; ++tick) {
        simulated_command.angles[1] =
            live_prediction_yaw(simulation, simulated_command, bindings, air_acceleration);
        simulated_command.movement.side_move = bindings.tuning.side_magnitude;
        simulate_live_movement_tick(simulation, bindings.collision, bindings.environment,
                                    bindings.player, simulated_command);
        if (simulation.simulation_flags & 2u)
            break;
        simulated_command.number =
            std::bit_cast<std::int32_t>(std::uint32_t(simulated_command.number) + 1u);
    }
    if (!(simulation.simulation_flags & 2u))
        return velocity_yaw;

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
        return velocity_yaw;
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
        return velocity_yaw;
    write_wall_heading(direction, forward, trace, movement_angles);
    return velocity_yaw;
}

} // namespace skeet
