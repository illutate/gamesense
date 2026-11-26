#include "skeet/movement/ground_contact.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
bool valid_plane(const MovementCollisionTrace& trace) noexcept {
    return trace.entity && trace.plane_normal.z >= 0.7f;
}
void trace_hull(MovementSimulation& state, MovementCollisionEngine& engine, MovementVector end,
                MovementVector minimum, MovementVector maximum, MovementCollisionTrace& trace) {
    engine.trace_ray(make_movement_hull_ray(state.position, end, minimum, maximum),
                     state.collision_mask,
                     {MovementCollisionFilterKind::skip_player, state.player, 8, 0}, trace);
}
void trace_ground(MovementSimulation& state, MovementCollisionEngine& engine, MovementVector end,
                  MovementCollisionTrace& trace) {
    using namespace movement_detail;
    trace_hull(state, engine, end, state.hull_minimum, state.hull_maximum, trace);
    if ((trace.fraction < 1.0f || trace.all_solid || trace.start_solid) && valid_plane(trace))
        return;
    const float fraction = trace.fraction;
    const MovementVector original_end = trace.end;
    const auto minimum = state.hull_minimum;
    const auto maximum = state.hull_maximum;
    MovementVector low_maximum{minimum_number(maximum.x, 0.0f), minimum_number(maximum.y, 0.0f),
                               maximum.z};
    MovementVector high_minimum{maximum_number(minimum.x, 0.0f), maximum_number(minimum.y, 0.0f),
                                minimum.z};
    trace_hull(state, engine, end, minimum, low_maximum, trace);
    if (!valid_plane(trace)) {
        trace_hull(state, engine, end, high_minimum, maximum, trace);
        if (!valid_plane(trace)) {
            trace_hull(state, engine, end, {minimum.x, high_minimum.y, minimum.z},
                       {low_maximum.x, maximum.y, maximum.z}, trace);
            if (!valid_plane(trace))
                trace_hull(state, engine, end, {high_minimum.x, minimum.y, minimum.z},
                           {maximum.x, low_maximum.y, maximum.z}, trace);
        }
    }
    trace.fraction = fraction;
    trace.end = original_end;
}
}

bool categorize_movement_ground(MovementSimulation& state, MovementCollisionEngine& engine,
                                bool extend_grounded_drop, bool mark_hard_landing) {
    state.surface_friction = 1.0f;
    const bool was_grounded = (state.player_flags & on_ground) != 0;
    const float vertical = state.velocity.z;
    bool moving_up = vertical > 140.0f;
    if (moving_up && was_grounded)
        moving_up = vertical - vertical > 140.0f;
    const bool extend = extend_grounded_drop && state.movement_type == 2 && was_grounded;
    MovementVector end = state.position;
    end.z -= 2.0f;
    if (extend)
        end.z -= state.rules.step_size;
    if (moving_up || (vertical > 0.0f && state.movement_type == 9)) {
        state.player_flags &= ~on_ground;
        return false;
    }
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = trace.hitbox = -1;
    trace_ground(state, engine, end, trace);
    const bool grounded =
        (trace.fraction < 1.0f || trace.all_solid || trace.start_solid) && valid_plane(trace);
    if (!grounded) {
        state.player_flags &= ~on_ground;
        if (vertical > 0.0f && state.movement_type != 8)
            state.surface_friction = 0.25f;
        return false;
    }
    if (mark_hard_landing && vertical < -580.0f)
        state.simulation_flags |= 0x10u;
    state.player_flags |= on_ground;
    state.surface_friction = engine.surface_friction(trace.surface_property) * 1.25f;
    if (state.surface_friction > 1.0f)
        state.surface_friction = 1.0f;
    if (state.movement_type != 8)
        state.velocity.z = 0.0f;
    if (extend && !trace.start_solid && trace.fraction > 0.0f && trace.fraction < 1.0f)
        state.position = trace.end;
    return true;
}

void prepare_movement_simulation_ground(MovementSimulation& state,
                                        MovementCollisionEngine& engine) {
    if (state.movement_type == 2) {
        if (state.velocity.z > 250.0f)
            state.player_flags &= ~on_ground;
        return;
    }
    categorize_movement_ground(state, engine, false);
}

} // namespace skeet
