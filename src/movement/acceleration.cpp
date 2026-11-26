#include "skeet/movement/acceleration.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

// 0x4339E713..0x4339E79B, including the tail call to minimum_number.
float air_acceleration_delta(MovementVector velocity, MovementVector direction, float wish_speed,
                             float acceleration, float interval) noexcept {
    const float capped_speed = minimum_number(wish_speed, 30.0f);
    const float additional = capped_speed - dot(velocity, direction);
    if (0.0f >= additional)
        return 0.0f;
    return minimum_number(additional, (wish_speed * acceleration) * interval);
}

// 0x433AC9C0..0x433ACDBA. The two out-of-line blocks at 0x433ACD94/DAF
// retain the initial 250-unit acceleration basis under their respective gates.
GroundAcceleration ground_acceleration(const MovementPhysics& physics, const UserCommand& command,
                                       MovementVector velocity, MovementVector direction,
                                       float wish_speed) noexcept {
    GroundAcceleration result{0.0f, 250.0f, physics.accelerate};
    const float current = dot(velocity, direction);
    const float additional = wish_speed - current;
    if (0.0f >= additional)
        return result;
    const bool duck =
        (command.buttons & duck_button) || physics.ducking || (physics.player_flags & 2);
    const bool walk = (command.buttons & speed_button) && !duck;
    float acceleration_speed = 250.0f;
    bool exception = false;
    if (physics.weapon_speed_enabled && physics.has_weapon) {
        exception = weapon_slowdown_exception(physics);
        const float scale = physics.weapon_max_speed * 0.004f;
        if (1.0f >= scale)
            result.wish_speed *= scale;
        if ((!duck && !walk) || exception) {
            if (1.0f >= scale)
                acceleration_speed = scale * 250.0f;
        }
    }
    if (duck) {
        if (!exception)
            acceleration_speed *= 0.34f;
        result.wish_speed *= 0.34f;
    }
    if (walk) {
        if (!exception)
            acceleration_speed *= 0.52f;
        result.wish_speed *= 0.52f;
        result.acceleration = walking_acceleration(result.acceleration, current, result.wish_speed);
    }
    result.amount = (physics.interval_per_tick * result.acceleration) * acceleration_speed;
    if (result.amount > additional)
        result.amount = additional;
    result.wish_speed = acceleration_speed;
    return result;
}

// 0x433EE5FC..0x433EEA5E. Predicts the next duck state before speed scaling.
GroundAccelerationCapacity ground_acceleration_capacity(const MovementPhysics& physics,
                                                        const UserCommand& command,
                                                        MovementVector velocity,
                                                        MovementVector direction) noexcept {
    bool duck = (command.buttons & duck_button) != 0;
    if (!duck) {
        bool transitioning = physics.ducking || physics.duck_amount > 0.0f;
        auto flags = physics.player_flags;
        if (transitioning || physics.duck_amount >= 1.0f) {
            const float duck_speed = physics.duck_speed >= 1.5f ? physics.duck_speed : 2.0f;
            const float remaining =
                sse_max(0.0f, physics.duck_amount - duck_speed * physics.interval_per_tick);
            transitioning = remaining > 0.0f && transitioning;
            if (!(remaining > 0.75f))
                flags = 0xfffffffdu;
        }
        duck = transitioning || (flags & 2);
    }
    const bool walk = (command.buttons & speed_button) && !duck;
    float limit = 250.0f;
    float acceleration_speed = 250.0f;
    bool exception = false;
    if (physics.weapon_speed_enabled && physics.has_weapon) {
        exception = weapon_slowdown_exception(physics);
        const float raw_scale = physics.weapon_max_speed * 0.004f;
        const float scale = 1.0f >= raw_scale ? raw_scale : 1.0f;
        float basis = 250.0f;
        if ((!duck && !walk) || exception) {
            acceleration_speed = scale * 250.0f;
            if (1.0f >= scale)
                basis = acceleration_speed;
        }
        limit = scale * basis;
    }
    if (duck) {
        if (!exception)
            acceleration_speed *= 0.34f;
        limit *= 0.34f;
    }
    float acceleration = physics.accelerate;
    if (walk) {
        if (!exception)
            acceleration_speed *= 0.52f;
        limit *= 0.52f;
        acceleration = walking_acceleration(acceleration, dot(velocity, direction), limit);
    }
    return {(acceleration * physics.interval_per_tick) * acceleration_speed, duck};
}

} // namespace skeet
