#include "skeet/movement/simulation_velocity.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/client/commands.hpp"
#include "skeet/movement/steering_math.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
using namespace movement_detail;
float length(MovementVector value) noexcept {
    return std::sqrt((value.x * value.x + value.y * value.y) + value.z * value.z);
}
void scale(MovementVector& value, float factor) noexcept {
    value.x *= factor;
    value.y *= factor;
    value.z *= factor;
}
MovementVector horizontal_normalize(MovementVector value) noexcept {
    value.z = 0.0f;
    const float inverse = 1.0f / (length(value) + epsilon);
    scale(value, inverse);
    return value;
}
float dot_native(MovementVector left, MovementVector right) noexcept {
    return (left.x * right.x + left.y * right.y) + left.z * right.z;
}
void add_acceleration(MovementVector& velocity, MovementVector direction, float amount) noexcept {
    velocity.x = direction.x * amount + velocity.x;
    velocity.y = direction.y * amount + velocity.y;
    velocity.z = direction.z * amount + velocity.z;
}
MovementVelocityPath ground_velocity(MovementSimulation& state, const MovementPhysics& snapshot,
                                     const UserCommand& command, MovementAngles angles,
                                     void* player, const MovementEnvironmentBindings* live,
                                     const client::Command* live_command) {
    state.velocity.z = 0.0f;
    state.fall_velocity = 0.0f;
    const float speed = length(state.velocity);
    if (speed >= 0.1f) {
        const float control = maximum_number(state.rules.stop_speed, speed);
        const float drop = (state.rules.friction * control) * state.rules.interval_per_tick;
        const float remaining = maximum_number(speed - drop, 0.0f);
        if (remaining != speed)
            scale(state.velocity, remaining / speed);
    }
    MovementVector forward, right;
    if (live_command)
        angles = {live_command->angles[0], live_command->angles[1], live_command->angles[2]};
    movement_angle_vectors(angles, &forward, &right, nullptr);
    if (forward.z != 0.0f)
        forward = horizontal_normalize(forward);
    if (right.z != 0.0f)
        right = horizontal_normalize(right);
    MovementVector direction{state.command_move.x * forward.x + state.command_move.y * right.x,
                             state.command_move.x * forward.y + state.command_move.y * right.y,
                             0.0f};
    float wish_speed = length(direction);
    const float inverse = 1.0f / (wish_speed + epsilon);
    scale(direction, inverse);
    if (wish_speed != 0.0f)
        wish_speed = sse_min(state.maximum_speed, wish_speed);
    state.velocity.z = 0.0f;
    const float current = dot_native(state.velocity, direction);
    const float additional = wish_speed - current;
    if (!(0.0f >= additional)) {
        float acceleration = state.rules.accelerate;
        MovementPhysics physics;
        bool duck, walk;
        if (live) {
            const auto captured =
                capture_movement_ground_weapon_physics(player, command, state.player_flags, *live);
            physics = captured.physics;
            duck = captured.duck;
            walk = captured.walk;
        } else {
            physics = snapshot;
            duck = (command.buttons & duck_button) || physics.ducking || (state.player_flags & 6);
            walk = (command.buttons & speed_button) && !duck;
        }
        const bool exception = physics.weapon_speed_enabled && physics.has_weapon &&
                               weapon_slowdown_exception(physics);
        const float basis = maximum_number(wish_speed, 250.0f);
        float walking_limit = basis;
        float acceleration_basis = basis;
        if (physics.weapon_speed_enabled && physics.has_weapon) {
            const float weapon_scale = physics.weapon_max_speed / 250.0f;
            if (1.0f > weapon_scale)
                walking_limit = weapon_scale * basis;
            if ((!duck && !walk) || exception) {
                if (1.0f >= weapon_scale)
                    acceleration_basis = weapon_scale * basis;
            }
        }
        if (duck) {
            if (!exception)
                acceleration_basis *= 0.34f;
            walking_limit *= 0.34f;
        }
        if (walk) {
            if (!exception)
                acceleration_basis *= 0.52f;
            walking_limit *= 0.52f;
            acceleration = walking_acceleration(acceleration, current, walking_limit);
        }
        const float amount = minimum_number(
            additional, ((state.rules.interval_per_tick * acceleration) * acceleration_basis) *
                            state.surface_friction);
        add_acceleration(state.velocity, direction, amount);
    }
    state.velocity.z = 0.0f;
    const float squared =
        (state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y) +
        state.velocity.z * state.velocity.z;
    if (squared > state.maximum_speed * state.maximum_speed) {
        const float divisor =
            std::sqrt((state.velocity.z * state.velocity.z + state.velocity.x * state.velocity.x) +
                      state.velocity.y * state.velocity.y);
        scale(state.velocity, state.maximum_speed / divisor);
    }
    if (1.0f > length(state.velocity)) {
        state.velocity = {0.0f, 0.0f, 0.0f};
        return MovementVelocityPath::ground_stopped;
    }
    return MovementVelocityPath::ground;
}
void air_velocity(MovementSimulation& state, const UserCommand& command, MovementAngles angles,
                  const client::Command* live_command) noexcept {
    MovementVector forward, right;
    if (live_command)
        angles = {live_command->angles[0], live_command->angles[1], live_command->angles[2]};
    movement_angle_vectors(angles, &forward, &right, nullptr);
    forward = horizontal_normalize(forward);
    right = horizontal_normalize(right);
    MovementVector direction{command.forward_move * forward.x + command.side_move * right.x,
                             command.forward_move * forward.y + command.side_move * right.y, 0.0f};
    float wish_speed = length(direction);
    const float inverse = 1.0f / (wish_speed + epsilon);
    scale(direction, inverse);
    if (wish_speed != 0.0f)
        wish_speed = sse_min(state.maximum_speed, wish_speed);
    const float capped = minimum_number(30.0f, wish_speed);
    const float additional = capped - dot_native(state.velocity, direction);
    if (!(0.0f >= additional)) {
        const float amount = minimum_number(
            additional, ((state.rules.cvar_dceaeac8 * wish_speed) * state.rules.interval_per_tick) *
                            state.surface_friction);
        add_acceleration(state.velocity, direction, amount);
    }
}
}

namespace {
MovementVelocityPath prepare_velocity(MovementSimulation& state, const MovementPhysics& physics,
                                      const UserCommand& command, MovementAngles angles,
                                      bool jump_speed_cap_enabled, void* player,
                                      const MovementEnvironmentBindings* live,
                                      const client::Command* live_command) {
    using namespace movement_detail;
    if (0.0f >= state.duck_amount)
        state.player_flags &= ~4u;
    if (!(state.simulation_flags & 1)) {
        state.simulation_flags |= 1;
        if ((state.buttons & duck_button) || state.ducking || (state.player_flags & 2)) {
            const float factor = (state.duck_amount * 0.34f + 1.0f) - state.duck_amount;
            scale(state.command_move, factor);
            state.maximum_speed *= factor;
            state.simulation_flags |= 1;
        }
    }
    if (state.movement_type != 2)
        return MovementVelocityPath::skip_walk;
    const float half_gravity = (state.rules.interval_per_tick * state.rules.gravity) * 0.5f;
    state.velocity.z -= half_gravity;
    if (state.buttons & jump_button) {
        state.simulation_flags &= ~0x40u;
        if (state.player_flags & on_ground) {
            if (live ? movement_jump_speed_cap(*live) : jump_speed_cap_enabled) {
                const float maximum = state.maximum_speed * 1.1f;
                if (0.0f < maximum) {
                    const float speed = length(state.velocity);
                    if (speed > maximum)
                        scale(state.velocity, maximum / speed);
                }
            }
            state.player_flags &= ~on_ground;
            if (!state.ducking && !(state.player_flags & 2))
                state.velocity.z += state.rules.jump_impulse;
            else
                state.velocity.z = state.rules.jump_impulse;
            const float previous_stamina = state.stamina;
            if (previous_stamina > 0.0f) {
                const float factor = sse_min(sse_max(1.0f - previous_stamina / 100.0f, 0.0f), 1.0f);
                state.velocity.z *= factor;
            }
            state.velocity.z -= half_gravity;
            state.stamina = sse_min(
                sse_max(state.rules.stamina_jump * state.rules.jump_impulse + previous_stamina,
                        0.0f),
                state.rules.stamina_maximum);
        }
    }
    if (state.player_flags & on_ground)
        return ground_velocity(state, physics, command, angles, player, live, live_command);
    air_velocity(state, command, angles, live_command);
    return MovementVelocityPath::air;
}
}

MovementVelocityPath prepare_movement_simulation_velocity(MovementSimulation& state,
                                                          const MovementPhysics& physics,
                                                          const UserCommand& command,
                                                          MovementAngles angles,
                                                          bool jump_speed_cap_enabled) noexcept {
    return prepare_velocity(state, physics, command, angles, jump_speed_cap_enabled, nullptr,
                            nullptr, nullptr);
}

MovementVelocityPath
prepare_live_movement_simulation_velocity(MovementSimulation& state, const UserCommand& command,
                                          MovementAngles angles, void* player,
                                          const MovementEnvironmentBindings& bindings) {
    return prepare_velocity(state, MovementPhysics{}, command, angles, false, player, &bindings,
                            nullptr);
}

MovementVelocityPath
prepare_live_movement_simulation_velocity(MovementSimulation& state, const client::Command& command,
                                          void* player,
                                          const MovementEnvironmentBindings& bindings) {
    return prepare_velocity(state, MovementPhysics{}, command.movement, {}, false, player,
                            &bindings, &command);
}

MovementVector trace_movement_simulation_ground(MovementSimulation& state,
                                                MovementCollisionEngine& engine) {
    const MovementVector end{state.velocity.x * state.rules.interval_per_tick + state.position.x,
                             state.velocity.y * state.rules.interval_per_tick + state.position.y,
                             state.position.z};
    engine.trace_ray(
        make_movement_hull_ray(state.position, end, state.hull_minimum, state.hull_maximum),
        state.collision_mask, {MovementCollisionFilterKind::skip_player, state.player, 8, 0},
        state.trace);
    return end;
}

void finish_movement_simulation_ground(MovementSimulation& state) noexcept {
    state.simulation_flags |= 0x40;
}
} // namespace skeet
