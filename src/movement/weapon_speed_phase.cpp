#include "skeet/movement/weapon_speed_phase.hpp"
#include "skeet/movement/acceleration.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/movement/speed_control_live.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cstring>
#include <limits>

namespace skeet {
namespace {
using namespace movement_detail;

float length(MovementVector value) noexcept {
    return std::sqrt((value.x * value.x + value.y * value.y) + value.z * value.z);
}

std::int32_t truncate_tick(float value) noexcept {
    // CVTTSS2SI produces the integer-indefinite value for NaN/overflow.
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

void add_duck(UserCommand& command, std::uint32_t scheduler_flags) noexcept {
    if ((scheduler_flags & 0x10040u) == 0x10000u)
        command.buttons |= duck_button;
}

struct SnapshotWeaponSpeed {
    const WeaponSpeedPhaseSettings& settings;
    const WeaponSpeedPhaseFrame& frame;
    std::uint32_t& flags;
    MovementCollisionEngine& collision;
    MovementAngles command_angles;

    bool blocked() const noexcept {
        return frame.weapon_blocked;
    }
    std::int32_t weapon_state() const noexcept {
        return frame.weapon_state;
    }
    std::uint32_t policy() const noexcept {
        return settings.policy_flags;
    }
    float guard() const noexcept {
        return frame.attack_guard_8815d240;
    }
    float current_time() const noexcept {
        return frame.current_time;
    }
    std::uint32_t player_flags() const noexcept {
        return frame.player.physics.player_flags;
    }
    std::uint16_t definition() const noexcept {
        return frame.weapon_definition;
    }
    float player_attack() const noexcept {
        return frame.player_next_attack;
    }
    float weapon_attack() const noexcept {
        return frame.weapon_next_attack;
    }
    float interval() const noexcept {
        return frame.player.physics.interval_per_tick;
    }
    MovementPhysics speed() const noexcept {
        return frame.player.physics;
    }
    MovementVector velocity() const noexcept {
        return frame.player.velocity;
    }
    MovementCommandFriction friction() const noexcept {
        return {frame.player.physics.stop_speed, frame.player.physics.friction, interval()};
    }
    void initialize(MovementSimulation& state, MovementVector velocity) const noexcept {
        initialize_movement_simulation(state, frame.player.entity, frame.position, velocity,
                                       frame.player.physics.player_flags, frame.simulation_mode,
                                       frame.snapshot, frame.rules);
    }
    void simulate(MovementSimulation& state, const client::Command& command) const {
        simulate_movement_tick(state, collision, frame.simulation_environment, frame.player.entity,
                               command.movement,
                               {command.angles[0], command.angles[1], command.angles[2]});
    }
    GroundAcceleration accelerate(const UserCommand& command, MovementVector velocity,
                                  MovementVector direction, float maximum) const noexcept {
        return ground_acceleration(frame.player.physics, command, velocity, direction, maximum);
    }
    void apply_regulator(client::Command& command, MovementAngleView angles,
                         float target) const noexcept {
        regulate_movement_speed(frame.player.physics, command.movement, velocity(), command_angles,
                                angles, target, true);
    }
    void counter(UserCommand& command, MovementAngleView angles) const noexcept {
        write_counter_movement(frame.player.physics, command, velocity(), angles);
    }
};

struct LiveWeaponSpeed {
    void* player;
    void* weapon;
    const MovementCommandBindings& bindings;
    std::uint32_t& flags;

    bool blocked() const noexcept {
        return movement_command_weapon_blocked(weapon, bindings);
    }
    std::int32_t weapon_state() const noexcept {
        return bindings.weapon_state;
    }
    std::uint32_t policy() const noexcept {
        return bindings.weapon_policy;
    }
    float guard() const noexcept {
        return movement_command_attack_guard(player, bindings);
    }
    float current_time() const noexcept {
        return bindings.current_time;
    }
    std::uint32_t player_flags() const noexcept {
        return movement_command_player_flags(player, bindings);
    }
    std::uint16_t definition() const noexcept {
        return movement_command_weapon_definition(weapon, bindings);
    }
    float player_attack() const noexcept {
        return movement_command_player_attack(player, bindings);
    }
    float weapon_attack() const noexcept {
        return movement_command_weapon_attack(weapon, bindings);
    }
    float interval() const noexcept {
        return *bindings.environment.interval;
    }
    MovementPhysics speed() const {
        return capture_movement_command_speed(player, bindings);
    }
    MovementVector velocity() const noexcept {
        return movement_command_velocity(player, bindings);
    }
    MovementCommandFriction friction() const noexcept {
        return capture_movement_command_friction(bindings);
    }
    void initialize(MovementSimulation& state, MovementVector velocity) const {
        // 4337241B/440/47C: mode, flags, then direct decoded origin; the
        // constructor owns its following cvar reads and 130/134/260 getters.
        const auto mode = field<std::uint32_t>(player, bindings.fields.simulation_mode_3d10b94c);
        const auto flags = player_flags();
        const auto position = field<MovementVector>(player, bindings.fields.origin_a9ee6062);
        initialize_live_movement_simulation(state, player, position, velocity, flags, mode,
                                            bindings.simulation);
    }
    void simulate(MovementSimulation& state, const client::Command& command) const {
        simulate_live_movement_tick(state, bindings.collision, bindings.environment, player,
                                    command);
    }
    GroundAcceleration accelerate(const UserCommand& command, MovementVector velocity,
                                  MovementVector direction, float maximum) const {
        return movement_command_ground_acceleration(player, command, velocity, direction, maximum,
                                                    bindings);
    }
    void apply_regulator(client::Command& command, MovementAngleView angles, float target) const {
        const auto current_velocity = velocity();
        regulate_live_movement_speed(player, command.movement, current_velocity,
                                     ConstMovementAngleView(command.angles), angles, target, true,
                                     bindings);
    }
    void counter(UserCommand& command, MovementAngleView angles) const {
        const auto current_velocity = velocity();
        write_live_movement_counter(player, command, current_velocity, angles, bindings);
    }
};

template <class Source>
void regulate(Source& source, client::Command& command, MovementAngleView movement_angles,
              bool crouch) {
    const auto physics = source.speed();
    float ceiling = minimum_number(physics.player_speed_limit, physics.player_max_speed) - 1.0f;
    if (crouch)
        ceiling *= 0.34f;
    const auto remainder = command.number % 1000;
    const float phase =
        (static_cast<float>(remainder) * source.interval()) * (crouch ? 7.0f : 10.0f);
    float wave = phase - static_cast<float>(std::floor(static_cast<double>(phase)));
    wave = absolute(wave - 0.5f);
    wave += wave;
    wave *= crouch ? 4.0f : 10.0f;
    source.apply_regulator(command, movement_angles, ceiling - wave);
}

// Returns true for the direct regulator branch at 43372AE5.
// Both input sources execute this single prediction/braking algorithm.
template <class Source>
bool should_regulate_before_attack(Source& source, std::uint32_t policy,
                                   const client::Command& command) {
    // 43372207/212/21C: player attack, interval, weapon attack.
    const float player_attack = source.player_attack();
    const float interval = source.interval();
    const float attack = maximum_number(source.weapon_attack(), player_attack);
    const float attack_tick = static_cast<float>(std::ceil(static_cast<double>(attack / interval)));
    const auto remaining = std::bit_cast<std::int32_t>(
        std::uint32_t(truncate_tick(attack_tick + 0.5f)) -
        std::uint32_t(truncate_tick(source.current_time() / interval + 0.5f)));
    float threshold = 0.0f;
    if (policy & 4u) {
        const auto physics = source.speed();
        threshold =
            (minimum_number(physics.player_speed_limit, physics.player_max_speed) - 1.0f) * 0.34f -
            2.0f;
    }
    if (remaining < 1)
        return false;

    const auto initial_velocity = source.velocity();
    auto copy = command;
    const float scale = 260.0f / (command_length(copy.movement) + epsilon);
    copy.movement.forward_move *= scale;
    copy.movement.side_move *= scale;
    copy.movement.up_move *= scale;
    MovementSimulation simulation{};
    source.initialize(simulation, initial_velocity);
    source.simulate(simulation, copy);
    auto velocity = simulation.velocity;
    // The first native norm adds z, y, x; later loop norms add x, y, z.
    float speed =
        std::sqrt((velocity.z * velocity.z + velocity.y * velocity.y) + velocity.x * velocity.x);
    if (speed < 1.0f)
        return true;
    const auto physics = source.speed();
    const float maximum = minimum_number(physics.player_speed_limit, physics.player_max_speed);
    const float friction_threshold = threshold + 0.1f;

    for (std::int32_t tick = 0; tick < remaining; ++tick) {
        const auto friction = source.friction();
        const float control = maximum_number(friction.stop_speed, speed);
        const float after_friction =
            maximum_number(speed - friction.interval * (control * friction.friction), 0.0f);
        if (after_friction < friction_threshold)
            return true;
        const float fraction = after_friction / speed;
        MovementVector friction_velocity{fraction * velocity.x, fraction * velocity.y,
                                         fraction * velocity.z};
        const MovementVector reverse{negate(velocity.x), negate(velocity.y), negate(velocity.z)};
        const float reciprocal = 1.0f / (length(reverse) + epsilon);
        MovementVector direction{reciprocal * reverse.x, reciprocal * reverse.y,
                                 reciprocal * reverse.z};
        if (direction.z != 0.0f) {
            const float horizontal =
                1.0f / (std::sqrt((direction.x * direction.x + direction.y * direction.y) + 0.0f) +
                        epsilon);
            direction = {direction.x * horizontal, direction.y * horizontal, horizontal * 0.0f};
        }

        auto acceleration = source.accelerate(copy.movement, friction_velocity, direction, maximum);
        float retry_speed = maximum;
        if (acceleration.amount > after_friction && acceleration.acceleration != 0.0f)
            retry_speed = 0.01f;
        if (retry_speed != maximum) {
            // 43372898/9E/B7 use ESP after one/one/three PUSHes: their
            // adjusted offsets still select velocity.x, copy, and player.
            acceleration =
                source.accelerate(copy.movement, friction_velocity, direction, retry_speed);
        }
        if (acceleration.amount > 0.0f) {
            friction_velocity.x = acceleration.amount * direction.x + friction_velocity.x;
            friction_velocity.y = acceleration.amount * direction.y + friction_velocity.y;
            friction_velocity.z = acceleration.amount * direction.z + friction_velocity.z;
        }
        velocity = friction_velocity;
        speed = length(velocity);
        if (speed < threshold + 1.0f)
            return true;
    }
    return false;
}

template <class Source>
WeaponSpeedPhaseContinuation process_phase(Source& source, client::Command& command,
                                           MovementAngleView movement_angles) {
    using Result = WeaponSpeedPhaseContinuation;
    auto& scheduler_flags = source.flags;
    if (source.blocked() || !(scheduler_flags & 8u))
        return Result::player_movement;
    const auto weapon_state = source.weapon_state();
    auto policy = source.policy();
    if (weapon_state != -1) {
        policy = (policy & 0xfffffff1u) | std::uint32_t(weapon_state);
        if ((policy & 2u) && (scheduler_flags & 0x1000u) && !(scheduler_flags & 0x40u)) {
            scheduler_flags |= 0x10000u;
            command.movement.buttons |= duck_button;
        }
    }
    if (!(policy & 0x10u)) {
        const float guard = source.guard();
        if (guard != 0.0f && absolute(source.current_time() - guard) < 1.0f)
            return Result::player_movement;
    }
    if ((scheduler_flags & 0x10u) && (source.player_flags() & on_ground)) {
        add_duck(command.movement, scheduler_flags);
        policy |= 4u;
    }

    if (policy & 8u) {
        if (weapon_state != -1 || (source.definition() != 0x40u &&
                                   should_regulate_before_attack(source, policy, command))) {
            regulate(source, command, movement_angles, false);
            return Result::complete;
        }
    }
    if (policy & 4u) {
        if (source.blocked())
            return Result::complete;
        add_duck(command.movement, scheduler_flags);
        regulate(source, command, movement_angles, true);
    } else if (source.player_flags() & on_ground) {
        add_duck(command.movement, scheduler_flags);
        source.counter(command.movement, movement_angles);
    }
    return Result::complete;
}

} // namespace

WeaponSpeedPhaseContinuation
process_weapon_speed_phase(const WeaponSpeedPhaseSettings& settings,
                           const WeaponSpeedPhaseFrame& frame, client::Command& command,
                           MovementAngles command_angles, MovementAngleView movement_angles,
                           std::uint32_t& scheduler_flags, MovementCollisionEngine& collision) {
    SnapshotWeaponSpeed source{settings, frame, scheduler_flags, collision, command_angles};
    return process_phase(source, command, movement_angles);
}

WeaponSpeedPhaseContinuation
process_live_weapon_speed_phase(void* player, void* weapon, client::Command& command,
                                MovementAngleView movement_angles,
                                const MovementCommandBindings& bindings) {
    LiveWeaponSpeed source{player, weapon, bindings, bindings.scheduler_flags};
    return process_phase(source, command, movement_angles);
}

} // namespace skeet
