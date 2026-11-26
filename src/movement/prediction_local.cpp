#include "skeet/movement/prediction.hpp"

namespace skeet {
MovementVector advance_movement_origin(std::uint32_t player, MovementVector position,
                                       MovementVector velocity, std::uint32_t player_flags,
                                       const MovementSimulationPlayer& snapshot,
                                       const MovementSimulationRules& rules,
                                       const MovementSimulationEnvironment& environment,
                                       const client::Command& command, std::uint32_t input_buttons,
                                       float yaw, MovementCollisionEngine& collision) {
    const float squared_input = command.movement.forward_move * command.movement.forward_move +
                                command.movement.side_move * command.movement.side_move;
    const float squared_velocity =
        (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
    if (!((input_buttons & 0x618u) && command.tick != 0x7f7fffff && squared_input > 10.0f) &&
        !(squared_velocity > 4.0f))
        return position;
    auto copy = command;
    copy.angles[1] = yaw;
    MovementSimulation simulation{};
    initialize_movement_simulation(simulation, player, position, velocity, player_flags, 0,
                                   snapshot, rules);
    const MovementAngles angles{copy.angles[0], copy.angles[1], copy.angles[2]};
    for (int i = 0; i < 2; ++i)
        simulate_movement_tick(simulation, collision, environment, player, copy.movement, angles);
    return simulation.position;
}
} // namespace skeet
