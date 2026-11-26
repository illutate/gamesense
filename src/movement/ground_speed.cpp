#include "skeet/movement/ground_speed.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

void process_ground_speed_tail(const GroundSpeedTailSettings& settings,
                               const MovementPhysics& physics, std::uint8_t player_state,
                               std::int32_t command_number, UserCommand& command,
                               MovementVector velocity, MovementAngles command_angles,
                               MovementAngleView movement_angles) noexcept {
    if (!(physics.player_flags & on_ground) || player_state >= 2 || (command.buttons & 0x2002u))
        return;

    if (settings.periodic_speed_enabled && settings.periodic_context_a &&
        settings.periodic_context_b) {
        const float ceiling =
            minimum_number(physics.player_speed_limit, physics.player_max_speed) - 1.0f;
        const float phase =
            (static_cast<float>(command_number % 1000) * physics.interval_per_tick) * 10.0f;
        float wave = phase - static_cast<float>(std::floor(static_cast<double>(phase)));
        wave = absolute(wave - 0.5f);
        wave += wave;
        wave *= 10.0f;
        regulate_movement_speed(physics, command, velocity, command_angles, movement_angles,
                                ceiling - wave, false);
    }

    // This gate reads the command after the preceding regulator has run.
    if (settings.fast_walk && !(command.buttons & duck_button) &&
        (command.buttons & speed_button)) {
        regulate_movement_speed(physics, command, velocity, command_angles, movement_angles, 134.0f,
                                true);
    }
}

} // namespace skeet
