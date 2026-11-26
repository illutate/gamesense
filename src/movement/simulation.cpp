#include "skeet/movement/simulation.hpp"

namespace skeet {

void initialize_movement_simulation(MovementSimulation& state, std::uint32_t player,
                                    MovementVector position, MovementVector velocity,
                                    std::uint32_t player_flags, std::uint32_t mode,
                                    const MovementSimulationPlayer& snapshot,
                                    const MovementSimulationRules& rules) noexcept {
    state.trace.hitgroup = -1;
    state.trace.hitbox = -1;
    state.player = player;
    state.trace.all_solid = 0;
    state.trace.start_solid = 0;
    state.trace.entity = 0;
    state.trace.fraction = 1.0f;
    state.rules = rules;
    state.position = position;
    state.velocity = velocity;
    state.player_flags = player_flags;
    state.mode = mode;
    state.speed_scale = snapshot.speed_scale;
    state.duck_speed = snapshot.duck_speed;
    state.duck_amount = snapshot.duck_amount;
    state.view_offset_z = snapshot.view_offset_z;
    state.stamina = snapshot.stamina;
    state.hull_minimum = snapshot.hull_minimum;
    state.hull_maximum = snapshot.hull_maximum;
    state.ducking = snapshot.ducking;
    state.fall_velocity = snapshot.fall_velocity;
    state.surface_friction = snapshot.surface_friction;
    state.movement_type = snapshot.movement_type;
    state.collision_mask = snapshot.collision_mask;
    state.simulation_flags =
        (state.simulation_flags & 0xbfu) | ((snapshot.field_f3c10e80_00 & 1u) << 6);
}

} // namespace skeet
