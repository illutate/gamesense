#include "skeet/movement/simulation.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

namespace {
void command_prefix(MovementSimulation& state, std::uint32_t player,
                    const UserCommand& command) noexcept {
    state.player = player;
    state.saved_vertical_velocity = state.velocity.z;
    state.buttons = command.buttons;
    state.command_move = {command.forward_move, command.side_move, command.up_move};
    state.state_5c = 0.0f;
    if (!(state.player_flags & on_ground))
        state.fall_velocity = negate(state.velocity.z);
    state.simulation_flags &= 0xc0u;
}
void command_speed(MovementSimulation& state, float player_speed_limit,
                   float player_max_speed) noexcept {
    float maximum = minimum_number(player_speed_limit, player_max_speed);
    if (state.buttons & speed_button) {
        const float walking_maximum = maximum * 0.52f;
        const float speed =
            std::sqrt((state.velocity.y * state.velocity.y + state.velocity.z * state.velocity.z) +
                      state.velocity.x * state.velocity.x);
        if (walking_maximum + 25.0f > speed)
            maximum = walking_maximum;
    }
    if (state.player_flags & on_ground)
        maximum *= state.speed_scale;
    if (state.stamina > 0.0f) {
        const float factor = sse_min(sse_max(1.0f - state.stamina / 100.0f, 0.0f), 1.0f);
        maximum = (factor * factor) * maximum;
    }
    state.maximum_speed = maximum;

    const float squared_input = (state.command_move.x * state.command_move.x +
                                 state.command_move.y * state.command_move.y) +
                                state.command_move.z * state.command_move.z;
    if (squared_input != 0.0f && squared_input > maximum * maximum) {
        const float scale = maximum / std::sqrt(squared_input);
        state.command_move.x *= scale;
        state.command_move.y *= scale;
        state.command_move.z *= scale;
    }
    if (state.stamina > 0.0f) {
        state.stamina -= state.rules.interval_per_tick * state.rules.stamina_recovery;
        if (0.0f > state.stamina)
            state.stamina = 0.0f;
    }
}
}

void prepare_movement_simulation_command(MovementSimulation& state, std::uint32_t player,
                                         float player_speed_limit, float player_max_speed,
                                         const UserCommand& command) noexcept {
    command_prefix(state, player, command);
    command_speed(state, player_speed_limit, player_max_speed);
}

void prepare_live_movement_simulation_command(MovementSimulation& state, void* player,
                                              const UserCommand& command,
                                              const MovementEnvironmentBindings& bindings) {
    command_prefix(state, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
                   command);
    const auto physics = capture_movement_command_physics(player, bindings);
    command_speed(state, physics.player_speed_limit, physics.player_max_speed);
}

} // namespace skeet
