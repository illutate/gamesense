#include "skeet/movement/simulation.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

void finish_movement_simulation_tick(MovementSimulation& state, bool landing_stamina_cvar_present,
                                     bool advance_vertical) noexcept {
    if (advance_vertical && !(state.player_flags & on_ground)) {
        state.velocity.z -= (state.rules.interval_per_tick * state.rules.gravity) * 0.5f;
        if (-580.0f > state.saved_vertical_velocity && state.velocity.z >= -580.0f)
            state.simulation_flags |= 0x10u;
    } else if (advance_vertical) {
        state.velocity.z = 0.0f;
        // COMISS zero, fall followed by JAE also enters on unordered input.
        if (!(0.0f >= state.fall_velocity)) {
            if (landing_stamina_cvar_present) {
                const float stamina =
                    state.rules.stamina_land * state.fall_velocity + state.stamina;
                state.stamina = sse_min(sse_max(stamina, 0.0f), state.rules.stamina_maximum);
            }
            state.fall_velocity = 0.0f;
        }
    }
    if (state.player_flags & on_ground) {
        if (1.0f > state.speed_scale) {
            const float scale = state.speed_scale - state.rules.interval_per_tick * -0.4f;
            state.speed_scale = sse_min(sse_max(scale, 0.0f), 1.0f);
        }
        state.fall_velocity = 0.0f;
    }
    ++state.mode;
}

} // namespace skeet
