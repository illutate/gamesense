#include "skeet/movement/slow_motion.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

bool process_slow_motion_stage(const SlowMotionSettings& settings, InputBitmap input,
                               const MovementPhysics& physics, std::int32_t weapon_state,
                               bool weapon_blocked, std::uint32_t& scheduler_flags,
                               std::int32_t command_number, UserCommand& command,
                               MovementVector velocity, MovementAngles command_angles,
                               MovementAngleView movement_angles) {
    if (!(settings.enabled && hotkey_active(settings.hotkey, input)) && !(scheduler_flags & 0x40u))
        return false;
    if ((scheduler_flags & 8u) && weapon_state != -1 && !(weapon_state & 4))
        return false;

    scheduler_flags = (scheduler_flags & 0xfffedfe7u) | ((scheduler_flags & 8u) << 10) | 0x80u;
    if (weapon_blocked)
        return true;

    const float ceiling =
        (minimum_number(physics.player_speed_limit, physics.player_max_speed) - 1.0f) * 0.34f;
    const float phase =
        (static_cast<float>(command_number % 1000) * physics.interval_per_tick) * 7.0f;
    float wave = phase - static_cast<float>(std::floor(static_cast<double>(phase)));
    wave = absolute(wave - 0.5f);
    wave += wave;
    wave *= 4.0f;
    regulate_movement_speed(physics, command, velocity, command_angles, movement_angles,
                            ceiling - wave, true);
    return true;
}

} // namespace skeet
