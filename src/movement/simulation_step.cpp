#include "skeet/movement/simulation_step.hpp"
#include "skeet/movement/simulation_velocity.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
void trace_step(MovementSimulation& state, MovementCollisionEngine& engine, MovementVector start,
                MovementVector end, MovementCollisionTrace& trace) {
    engine.trace_ray(make_movement_hull_ray(start, end, state.hull_minimum, state.hull_maximum),
                     state.collision_mask,
                     {MovementCollisionFilterKind::skip_player, state.player, 8, 0}, trace);
}

void retain_ground(MovementSimulation& state, MovementCollisionEngine& engine) {
    MovementVector upper = state.position, lower = state.position;
    upper.z += 2.0f;
    lower.z -= state.rules.step_size;
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    trace_step(state, engine, state.position, upper, trace);
    upper = trace.end;
    trace_step(state, engine, upper, lower, trace);
    if (trace.fraction > 0.0f && trace.fraction < 1.0f && !trace.start_solid &&
        static_cast<double>(trace.plane_normal.z) >= 0.7 &&
        movement_detail::absolute(state.position.z - trace.end.z) > 0.015625f)
        state.position = trace.end;
}

void step_move(MovementSimulation& state, MovementCollisionEngine& engine,
               MovementVector first_destination) {
    const auto original_position = state.position;
    const auto original_velocity = state.velocity;
    slide_movement_simulation(state, engine, &first_destination);
    const auto flat_position = state.position;
    const auto flat_velocity = state.velocity;
    state.position = original_position;
    state.velocity = original_velocity;
    MovementVector upper = state.position;
    upper.z = static_cast<float>((static_cast<double>(state.rules.step_size) + 0.03125) +
                                 static_cast<double>(upper.z));
    trace_step(state, engine, state.position, upper, state.trace);
    if (!state.trace.start_solid && !state.trace.all_solid)
        state.position = state.trace.end;
    slide_movement_simulation(state, engine);
    MovementVector lower = state.position;
    lower.z = static_cast<float>(static_cast<double>(lower.z) -
                                 (static_cast<double>(state.rules.step_size) + 0.03125));
    trace_step(state, engine, state.position, lower, state.trace);
    if (static_cast<double>(state.trace.plane_normal.z) < 0.7) {
        state.position = flat_position;
        state.velocity = flat_velocity;
        return;
    }
    if (!state.trace.start_solid && !state.trace.all_solid)
        state.position = state.trace.end;
    const float flat_x = flat_position.x - original_position.x;
    const float flat_y = flat_position.y - original_position.y;
    const float step_x = state.position.x - original_position.x;
    const float step_y = state.position.y - original_position.y;
    if (flat_y * flat_y + flat_x * flat_x > step_x * step_x + step_y * step_y) {
        state.position = flat_position;
        state.velocity = flat_velocity;
    } else {
        state.velocity.z = flat_velocity.z;
    }
}
}

void move_movement_simulation_ground(MovementSimulation& state, MovementCollisionEngine& engine) {
    const auto destination = trace_movement_simulation_ground(state, engine);
    if (state.trace.fraction == 1.0f) {
        state.position = state.trace.end;
    } else {
        if (!(state.player_flags & on_ground)) {
            finish_movement_simulation_ground(state);
            return;
        }
        step_move(state, engine, destination);
    }
    retain_ground(state, engine);
    finish_movement_simulation_ground(state);
}

} // namespace skeet
