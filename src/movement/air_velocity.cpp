#include "skeet/movement/air_velocity.hpp"
#include "skeet/movement/bunny_hop.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

AirVelocityPreparation
prepare_air_strafe_velocity(MovementVector velocity, float stamina, const MovementPhysics& physics,
                            const MovementSimulationRules& rules, bool movement_keys,
                            bool bunny_hop_enabled, bool jump_speed_cap_enabled,
                            float side_magnitude, UserCommand& command) noexcept {
    velocity.z -= (rules.gravity * 0.5f) * rules.interval_per_tick;
    if (!(physics.player_flags & on_ground))
        return {velocity, stamina, true};
    if (!(command.buttons & jump_button))
        return {velocity, stamina, false};
    const float squared = velocity.y * velocity.y + velocity.x * velocity.x;
    if (4.0f > squared) {
        recover_bunny_hop_low_speed(bunny_hop_enabled, {1.0f, false, false}, velocity, true,
                                    movement_keys, command, side_magnitude);
        return {velocity, stamina, false};
    }
    if (stamina > 0.0f)
        stamina = sse_max(0.0f, stamina - rules.stamina_recovery * rules.interval_per_tick);
    if (jump_speed_cap_enabled) {
        const float limit = physics.player_speed_limit * 1.1f;
        const float speed = std::sqrt((velocity.x * velocity.x + velocity.y * velocity.y) +
                                      velocity.z * velocity.z);
        if (speed > limit) {
            const float scale = limit / speed;
            velocity.z *= scale;
            velocity.x *= scale;
            velocity.y *= scale;
        }
    }
    if (physics.ducking || (physics.player_flags & 2u))
        velocity.z = rules.jump_impulse;
    else
        velocity.z += rules.jump_impulse;
    if (stamina > 0.0f) {
        const float scale = sse_min(sse_max(1.0f - stamina / 100.0f, 0.0f), 1.0f);
        velocity.z *= scale;
    }
    velocity.z -= (rules.gravity * 0.5f) * rules.interval_per_tick;
    return {velocity, stamina, true};
}

} // namespace skeet
