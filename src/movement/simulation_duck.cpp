#include "skeet/movement/simulation_duck.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/movement/ground_contact.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
float approach(float target, float value, float step) noexcept {
    const float difference = target - value;
    if (difference > step)
        return value + step;
    if (movement_detail::negate(step) > difference)
        return value - step;
    return target;
}
void update_duck(MovementSimulation& state, MovementCollisionEngine& engine, bool field_a15c3105,
                 void* player, const MovementEnvironmentBindings* live) {
    const bool grounded = (state.player_flags & 1u) != 0;
    state.duck_speed = approach(8.0f, state.duck_speed, state.rules.interval_per_tick * 3.0f);
    if ((state.buttons & 4u) == 0) {
        if (state.duck_amount > 0.0f)
            state.ducking = 1;
        else if (!state.ducking)
            return;
        if (grounded)
            return;
        float rate = maximum_number(1.5f, state.duck_speed);
        if (live ? movement_player_duck_slowdown(player, *live) : field_a15c3105)
            rate *= 0.4f;
        state.duck_amount = approach(0.0f, state.duck_amount, state.rules.interval_per_tick * rate);
        if ((state.player_flags & 1u) == 0 && state.movement_type != 9)
            state.position.z -= 9.0f;
        state.player_flags &= ~6u;
        state.ducking = 0;
        state.view_offset_z = 64.0f;
        categorize_movement_ground(state, engine, true, true);
        state.duck_amount = 0.0f;
        state.player_flags &= ~6u;
        return;
    }
    if (1.0f > state.duck_amount)
        state.ducking = 1;
    if (!state.ducking)
        return;
    float rate = state.duck_speed * 0.8f;
    if (live ? movement_player_duck_slowdown(player, *live) : field_a15c3105)
        rate *= 0.4f;
    state.duck_amount = approach(1.0f, state.duck_amount, rate * state.rules.interval_per_tick);
    if (state.duck_amount < 1.0f && grounded) {
        const float square = state.duck_amount * state.duck_amount;
        const float twice = square + square;
        const float eased = square * 3.0f - twice * state.duck_amount;
        state.view_offset_z = (1.0f - eased) * 18.0f + 46.0f;
    } else {
        if (!grounded && state.movement_type != 9)
            state.position.z += 9.0f;
        state.view_offset_z = 46.0f;
        state.ducking = 0;
        state.player_flags |= 6u;
        categorize_movement_ground(state, engine, true, true);
        state.duck_amount = 1.0f;
    }
    if (state.duck_amount >= 0.1f)
        state.player_flags |= 4u;
}
}
void update_movement_simulation_duck(MovementSimulation& state, MovementCollisionEngine& engine,
                                     bool field_a15c3105) {
    update_duck(state, engine, field_a15c3105, nullptr, nullptr);
}
void update_live_movement_simulation_duck(MovementSimulation& state,
                                          MovementCollisionEngine& engine, void* player,
                                          const MovementEnvironmentBindings& bindings) {
    update_duck(state, engine, false, player, &bindings);
}
} // namespace skeet
