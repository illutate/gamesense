#include "skeet/movement/normalized_air_final.hpp"
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

std::int32_t truncate_integer(float value) noexcept {
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

float effective_maximum_speed(float stamina, const UserCommand& command,
                              const NormalizedAirFinalBindings& bindings) {
    // The virtual getter precedes all property reads at every invocation.
    const float maximum = engine::invoke<float>(bindings.player, 0x450u / 4u);
    float result = minimum_number(
        field<float>(bindings.player, bindings.environment.fields.speed_limit_3eb71843), maximum);
    if (stamina > 0.0f) {
        const float scale = sse_min(sse_max(1.0f - stamina / 100.0f, 0.0f), 1.0f);
        result *= scale * scale;
    }
    const auto& fields = bindings.environment.fields;
    if ((command.buttons & duck_button) ||
        field<std::uint8_t>(bindings.player, fields.ducking_6b1a59b5) ||
        (field<std::uint8_t>(bindings.player, fields.player_flags_e2fbe695) & 2u)) {
        const float amount = field<float>(bindings.player, fields.duck_amount_3309c0cf);
        const float scale =
            (amount * 0.34f + 1.0f) - field<float>(bindings.player, fields.duck_amount_3309c0cf);
        result *= scale;
    }
    return result;
}

struct SteeringAngles {
    float minimum, wide;
};

SteeringAngles steering_angles(const NormalizedAirFinalSettings& settings, float speed,
                               float maximum, const MovementSimulation& simulation,
                               const NormalizedAirFinalBindings& bindings) {
    const float first_friction = simulation.surface_friction;
    const float first_basis = maximum_number(settings.acceleration_limit, speed);
    const float first_acceleration =
        ((convar_float(bindings.air_acceleration) * maximum) * *bindings.environment.interval) *
        first_friction;
    const float first_cosine = sse_min(
        sse_max((settings.acceleration_limit - first_acceleration) / first_basis, -1.0f), 1.0f);
    const float minimum =
        minimum_number(std::acos(first_cosine) * radians_to_degrees, settings.maximum_angle);
    const float second_friction = simulation.surface_friction;
    const float second_basis = maximum_number(settings.acceleration_limit, speed);
    const float second_acceleration =
        ((convar_float(bindings.air_acceleration) * maximum) * *bindings.environment.interval) *
        second_friction;
    const float second_cosine =
        sse_min(sse_max(negate(minimum_number(settings.angle_step, second_acceleration) /
                               (second_basis * 2.0f)),
                        -1.0f),
                1.0f);
    return {minimum, std::acos(second_cosine) * radians_to_degrees};
}

SteeringAngles live_steering_angles(const NormalizedAirFinalSettings& settings, float speed,
                                    float maximum, const NormalizedAirFinalBindings& bindings) {
    // The two reads bracket the first acos/minimum operation in the native block.
    const float first_friction =
        field<float>(bindings.player, bindings.capture.offsets.field_f3c10e80 + 0x14u);
    const float first_basis = maximum_number(settings.acceleration_limit, speed);
    const float first_acceleration =
        ((convar_float(bindings.air_acceleration) * maximum) * *bindings.environment.interval) *
        first_friction;
    const float first_cosine = sse_min(
        sse_max((settings.acceleration_limit - first_acceleration) / first_basis, -1.0f), 1.0f);
    const float minimum =
        minimum_number(std::acos(first_cosine) * radians_to_degrees, settings.maximum_angle);
    const float second_friction =
        field<float>(bindings.player, bindings.capture.offsets.field_f3c10e80 + 0x14u);
    const float second_basis = maximum_number(settings.acceleration_limit, speed);
    const float second_acceleration =
        ((convar_float(bindings.air_acceleration) * maximum) * *bindings.environment.interval) *
        second_friction;
    const float second_cosine =
        sse_min(sse_max(negate(minimum_number(settings.angle_step, second_acceleration) /
                               (second_basis * 2.0f)),
                        -1.0f),
                1.0f);
    return {minimum, std::acos(second_cosine) * radians_to_degrees};
}

bool surface_blocks_steering(void* surface_properties, std::int16_t property) {
    const auto* surface =
        engine::invoke<const std::uint8_t*>(surface_properties, 5, std::int32_t(property));
    return surface && surface[0x62] != 0;
}

bool forecast_collision(const MovementSimulation& simulation, MovementVector previous_position,
                        const NormalizedAirFinalBindings& bindings) {
    MovementTraceFlagGuard guard(bindings.environment.engine_trace_flag);
    const MovementVector displacement{simulation.contact_position.x - previous_position.x,
                                      simulation.contact_position.y - previous_position.y,
                                      simulation.contact_position.z - previous_position.z};
    const float distance =
        std::sqrt((displacement.z * displacement.z + displacement.y * displacement.y) +
                  displacement.x * displacement.x);
    const float scale = (distance + 24.0f) / (distance + epsilon);
    const MovementVector extended{displacement.x * scale, displacement.y * scale,
                                  displacement.z * scale};
    const MovementVector maximum =
        *engine::invoke<const MovementVector*>(bindings.player, 0x134u / 4u);
    const MovementVector start{previous_position.x, previous_position.y,
                               previous_position.z + 18.0f};
    const MovementVector end{extended.x + previous_position.x, extended.y + previous_position.y,
                             extended.z + start.z};
    const MovementVector minimum =
        *engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    const MovementVector top{maximum.x, maximum.y, 24.0f};
    const MovementCollisionFilter filter{MovementCollisionFilterKind::reject_entities, 0, 0, 0};
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    bindings.collision.trace_ray(make_movement_hull_ray(start, end, minimum, top), 0x200400bu,
                                 filter, trace);
    if (trace.plane_normal.z != 1.0f &&
        ((trace.contents & 0x20000000u) ||
         surface_blocks_steering(bindings.surface_properties, trace.surface_property)))
        return false;
    const auto mask = engine::invoke<std::uint32_t>(bindings.player, 0x260u / 4u) & 0xfffeffffu;
    const MovementVector second_minimum =
        *engine::invoke<const MovementVector*>(bindings.player, 0x130u / 4u);
    bindings.collision.trace_ray(make_movement_hull_ray(start, end, second_minimum, top), mask,
                                 filter, trace);
    return (1.0f > trace.fraction || trace.all_solid || trace.start_solid) &&
           0.1f > absolute(trace.plane_normal.z) &&
           !ballistic_breakable(bindings.ballistics, trace.entity);
}

bool forecast_turn(const NormalizedAirFinalSettings& settings, const NormalizedAirFinalInput& input,
                   const client::Command& command, ConstMovementAngleView movement_angles,
                   std::int32_t ticks, bool positive, const NormalizedAirFinalBindings& bindings) {
    const auto flags =
        field<std::uint32_t>(bindings.player, bindings.environment.fields.player_flags_e2fbe695);
    MovementSimulation simulation{};
    initialize_live_movement_simulation(simulation, bindings.player, input.position, input.velocity,
                                        flags, 0, bindings.capture);
    auto copy = command;
    copy.angles = {movement_angles.pitch, movement_angles.yaw, movement_angles.roll};
    for (std::int32_t tick = 0; tick < ticks; ++tick) {
        const auto previous_position = simulation.position;
        const float speed = horizontal_length(simulation.velocity);
        const float yaw = planar_movement_yaw(simulation.velocity);
        static_cast<void>(normalize_movement_angle(movement_angles.yaw - yaw));
        const float smoothing =
            sse_min(sse_max(1.0f - static_cast<float>(settings.smoothing) * 0.01f, 0.0f), 1.0f);
        const float maximum = effective_maximum_speed(simulation.stamina, copy.movement, bindings);
        const auto angles = steering_angles(settings, speed, maximum, simulation, bindings);
        const float step = (angles.wide - angles.minimum) * smoothing + angles.minimum;
        copy.angles[1] = positive ? yaw + step : yaw - step;
        copy.movement.forward_move = settings.forward_magnitude;
        simulate_live_movement_tick(simulation, bindings.collision, bindings.environment,
                                    bindings.player, copy);
        if (simulation.simulation_flags & 2u)
            return forecast_collision(simulation, previous_position, bindings);
        copy.number = std::bit_cast<std::int32_t>(std::uint32_t(copy.number) + 1u);
    }
    return false;
}

} // namespace

void finish_normalized_air_strafe(const NormalizedAirFinalSettings& settings,
                                  const NormalizedAirFinalInput& input,
                                  const MovementFeedbackHistory& history,
                                  MovementFeedback& feedback, client::Command& command,
                                  MovementAngleView movement_angles,
                                  const NormalizedAirFinalBindings& bindings) {
    using namespace movement_detail;
    const float difference =
        absolute(normalize_movement_angle(movement_angles.yaw - input.velocity_yaw));
    const float maximum = effective_maximum_speed(input.stamina, command.movement, bindings);
    const auto angles = live_steering_angles(settings, input.speed, maximum, bindings);
    float ideal = angles.minimum;
    if (input.speed >= settings.maximum_angle) {
        const float smoothing =
            sse_min(sse_max(static_cast<float>(settings.smoothing) * 0.01f, 0.0f), 1.0f);
        ideal = (angles.minimum - angles.wide) * smoothing + angles.wide;
    }
    feedback.primary_heading = normalize_movement_angle(movement_angles.yaw);
    float selected = std::numeric_limits<float>::max();
    if (input.selection.pre_speed_requested && !input.selection.pre_speed_effective &&
        input.speed > settings.maximum_angle) {
        selected = angles.minimum;
    } else if ((input.selection.movement_key_mode & 0xffu) && difference > settings.maximum_angle &&
               input.speed > settings.maximum_angle) {
        if (history.secondary_heading != std::numeric_limits<float>::max() &&
            settings.acceleration_limit > absolute(normalize_movement_angle(
                                              history.secondary_heading - movement_angles.yaw))) {
            feedback.secondary_heading = history.secondary_heading;
            feedback.turn_direction = history.turn_direction;
            selected = ideal * history.turn_direction;
        } else {
            const auto ticks = truncate_integer(0.5f / (*bindings.environment.interval) + 0.5f);
            const bool negative_blocked =
                forecast_turn(settings, input, command, movement_angles, ticks, false, bindings);
            const bool positive_blocked =
                forecast_turn(settings, input, command, movement_angles, ticks, true, bindings);
            if (negative_blocked != positive_blocked) {
                feedback.secondary_heading = movement_angles.yaw;
                feedback.turn_direction = negative_blocked ? 1.0f : -1.0f;
                selected = ideal * feedback.turn_direction;
            } else if (difference > 160.0f) {
                selected = ideal;
            }
        }
    }
    // The native UCOMISS gate accepts unordered candidates and rejects only
    // the ordered FLT_MAX sentinel before considering the nearest heading.
    if (selected == std::numeric_limits<float>::max()) {
        selected = difference > 20.0f ? ideal : angles.minimum;
        const float negative = absolute(
            normalize_movement_angle((input.velocity_yaw - selected) - movement_angles.yaw));
        const float positive = absolute(
            normalize_movement_angle((selected + input.velocity_yaw) - movement_angles.yaw));
        if (positive > negative)
            selected = negate(selected);
    }
    movement_angles.yaw = input.velocity_yaw + selected;
    command.movement.forward_move = settings.forward_magnitude;
    movement_angles.yaw = normalize_movement_angle(movement_angles.yaw);
}

} // namespace skeet
