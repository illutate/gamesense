#include "skeet/anti_aim/movement_capture.hpp"
#include "skeet/movement/simulation_environment.hpp"

namespace skeet::anti_aim {
MovementVector advance_live_movement_origin(void* player, MovementVector position,
                                            MovementVector velocity, std::uint32_t player_flags,
                                            const MovementCaptureInput& input,
                                            const MovementEnvironmentBindings& environment,
                                            const client::Command& command,
                                            std::uint32_t input_buttons, float yaw,
                                            MovementCollisionEngine& collision) {
    const auto movement_squared = command.movement.forward_move * command.movement.forward_move +
                                  command.movement.side_move * command.movement.side_move;
    const auto velocity_squared =
        (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
    if (!((input_buttons & 0x618u) && command.tick != 0x7FFFFFFF && movement_squared > 10.0f) &&
        !(velocity_squared > 4.0f))
        return position;
    auto copy = command;
    copy.angles[1] = yaw;
    MovementSimulation simulation{};
    initialize_live_movement_simulation(simulation, player, position, velocity, player_flags, 0,
                                        input);
    for (int tick = 0; tick != 2; ++tick)
        simulate_live_movement_tick(simulation, collision, environment, player, copy);
    return simulation.position;
}
} // namespace skeet::anti_aim
