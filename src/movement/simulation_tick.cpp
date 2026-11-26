#include "skeet/movement/simulation_tick.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/client/commands.hpp"
#include "skeet/movement/ground_contact.hpp"
#include "skeet/movement/simulation_duck.hpp"
#include "skeet/movement/simulation_velocity.hpp"
#include "skeet/movement/simulation_step.hpp"
#include "movement_math.hpp"

namespace skeet {

void simulate_movement_tick(MovementSimulation& state, MovementCollisionEngine& engine,
                            const MovementSimulationEnvironment& environment, std::uint32_t player,
                            const UserCommand& command, MovementAngles command_angles) {
    MovementTraceFlagGuard trace_flag(environment.engine_trace_flag);
    prepare_movement_simulation_command(state, player, environment.physics.player_speed_limit,
                                        environment.physics.player_max_speed, command);
    prepare_movement_simulation_ground(state, engine);
    if (!(state.player_flags & on_ground))
        state.fall_velocity = movement_detail::negate(state.velocity.z);
    update_movement_simulation_duck(state, engine, environment.player_field_a15c3105);
    const auto path = prepare_movement_simulation_velocity(
        state, environment.physics, command, command_angles, environment.jump_speed_cap_enabled);
    if (path == MovementVelocityPath::skip_walk) {
        finish_movement_simulation_tick(state, environment.landing_stamina_cvar_present, false);
        return;
    }
    if (path == MovementVelocityPath::ground)
        move_movement_simulation_ground(state, engine);
    else if (path == MovementVelocityPath::ground_stopped)
        finish_movement_simulation_ground(state);
    else
        slide_movement_simulation(state, engine);
    categorize_movement_ground(state, engine, true);
    finish_movement_simulation_tick(state, environment.landing_stamina_cvar_present);
}

namespace {
void live_tick(MovementSimulation& state, MovementCollisionEngine& engine,
               const MovementEnvironmentBindings& bindings, void* player,
               const UserCommand& command, MovementAngles angles,
               const client::Command* live_command) {
    MovementTraceFlagGuard trace_flag(bindings.engine_trace_flag);
    prepare_live_movement_simulation_command(state, player, command, bindings);
    prepare_movement_simulation_ground(state, engine);
    if (!(state.player_flags & on_ground))
        state.fall_velocity = movement_detail::negate(state.velocity.z);
    update_live_movement_simulation_duck(state, engine, player, bindings);
    const auto path =
        live_command
            ? prepare_live_movement_simulation_velocity(state, *live_command, player, bindings)
            : prepare_live_movement_simulation_velocity(state, command, angles, player, bindings);
    if (path == MovementVelocityPath::skip_walk) {
        finish_movement_simulation_tick(state, false, false);
        return;
    }
    if (path == MovementVelocityPath::ground)
        move_movement_simulation_ground(state, engine);
    else if (path == MovementVelocityPath::ground_stopped)
        finish_movement_simulation_ground(state);
    else
        slide_movement_simulation(state, engine);
    categorize_movement_ground(state, engine, true);
    bool landing_stamina_present = false;
    if ((state.player_flags & on_ground) && !(0.0f >= state.fall_velocity))
        landing_stamina_present = movement_landing_stamina_present(bindings);
    finish_movement_simulation_tick(state, landing_stamina_present);
}
}

void simulate_live_movement_tick(MovementSimulation& state, MovementCollisionEngine& engine,
                                 const MovementEnvironmentBindings& bindings, void* player,
                                 const UserCommand& command, MovementAngles angles) {
    live_tick(state, engine, bindings, player, command, angles, nullptr);
}

void simulate_live_movement_tick(MovementSimulation& state, MovementCollisionEngine& engine,
                                 const MovementEnvironmentBindings& bindings, void* player,
                                 const client::Command& command) {
    live_tick(state, engine, bindings, player, command.movement, {}, &command);
}

} // namespace skeet
