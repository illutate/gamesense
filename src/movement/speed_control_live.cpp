#include "skeet/movement/speed_control_live.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
namespace {
using namespace movement_detail;

template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

bool self_integer(const void* object) noexcept {
    for (;;) {
        const auto* const parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30u) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}

//433EE7D7..433EE994. Duck/walk decisions have already been retained before
// the entity and weapon methods; those methods can change live player data.
void capture_capacity_weapon(void* player, MovementPhysics& physics,
                             const MovementEnvironmentBindings& bindings) {
    physics.weapon_speed_enabled = !self_integer(bindings.convars.accelerate_weapon_speed);
    if (!physics.weapon_speed_enabled)
        return;
    const auto handle = field<std::int32_t>(player, bindings.fields.weapon_handle_cd812654);
    if (handle == -1)
        return;
    auto* const weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
    if (!weapon)
        return;
    physics.has_weapon = true;
    physics.weapon_max_speed = engine::invoke<float>(weapon, 0x6E8u / 4u);
    physics.weapon_mode_count = engine::invoke<std::int32_t>(weapon, 0x738u / 4u);
    if (physics.weapon_mode_count > 0)
        physics.weapon_mode = engine::invoke<std::int32_t>(weapon, 0x6ECu / 4u);
}
}

GroundAccelerationCapacity
live_movement_acceleration_capacity(void* player, const UserCommand& command,
                                    MovementVector velocity, MovementVector direction,
                                    const MovementCommandBindings& bindings) {
    const auto& environment = bindings.environment;
    // These three reads are unconditional, including for command IN_DUCK.
    const bool raw_duck = field<std::uint8_t>(player, environment.fields.ducking_6b1a59b5) != 0;
    const float amount = field<float>(player, environment.fields.duck_amount_3309c0cf);
    auto flags = field<std::uint32_t>(player, environment.fields.player_flags_e2fbe695);
    bool duck = (command.buttons & duck_button) != 0;
    if (!duck) {
        bool transitioning = raw_duck || amount > 0.0f;
        if (transitioning || amount >= 1.0f) {
            const float raw_speed = field<float>(player, environment.fields.duck_speed_fb7e3404);
            const float duck_speed = raw_speed >= 1.5f ? raw_speed : 2.0f;
            const float remaining = sse_max(0.0f, amount - duck_speed * (*environment.interval));
            const bool positive_or_unordered = !(remaining <= 0.0f);
            transitioning = positive_or_unordered && transitioning;
            if (!positive_or_unordered || remaining <= 0.75f)
                flags = 0xfffffffdu;
        }
        duck = transitioning || (flags & 2u);
    }
    const bool walk = (command.buttons & speed_button) && !duck;

    MovementPhysics physics{};
    capture_capacity_weapon(player, physics, environment);
    physics.accelerate = movement_command_convar_float(environment.convars.accelerate);
    physics.interval_per_tick = *environment.interval;

    // The numerical helper receives the already decided native booleans.
    // It must not predict duck a second time using the later interval.
    UserCommand numerical_command{};
    numerical_command.buttons = (duck ? duck_button : 0u) | (walk ? speed_button : 0u);
    return ground_acceleration_capacity(physics, numerical_command, velocity, direction);
}

void regulate_live_movement_speed(void* player, UserCommand& command, MovementVector velocity,
                                  ConstMovementAngleView command_angles,
                                  MovementAngleView movement_angles, float target_speed,
                                  bool clear_speed_on_acceleration,
                                  const MovementCommandBindings& bindings) {
    const auto initial_buttons = command.buttons;
    if ((initial_buttons & jump_button) || command_length(command) < 38.0f)
        return;
    const float speed = horizontal_length(velocity);
    if (speed < 28.0f)
        return;
    const auto friction = capture_movement_command_friction(bindings);
    const float remaining =
        maximum_number(speed - (maximum_number(friction.stop_speed, speed) * friction.friction) *
                                   friction.interval,
                       0.0f);
    const float fraction = remaining / speed;
    const MovementVector friction_velocity{velocity.x * fraction, velocity.y * fraction,
                                           fraction * 0.0f};
    float difference = target_speed - remaining;
    if (absolute(difference) < 0.5f) {
        command.side_move = 0.0f;
        command.forward_move = 0.0f;
        return;
    }

    MovementVector direction;
    if (difference < 0.0f) {
        direction = normalize_horizontal({negate(velocity.x), negate(velocity.y), 0.0f});
        const auto angles = horizontal_angles(direction, true);
        movement_angles.yaw = angles.yaw;
        movement_angles.pitch = angles.pitch;
        movement_angles.roll = 0.0f;
        difference = negate(difference);
        command.side_move = 0.0f;
        command.forward_move = 450.0f;
    } else {
        if (clear_speed_on_acceleration)
            command.buttons = initial_buttons & ~speed_button;
        MovementVector forward, right;
        command_basis(static_cast<MovementAngles>(command_angles), forward, right);
        if (forward.z != 0.0f)
            forward = normalize_horizontal(forward);
        if (right.z != 0.0f)
            right = normalize_horizontal(right);
        direction = normalize_horizontal(
            {command.forward_move * forward.x + command.side_move * right.x,
             command.side_move * right.y + command.forward_move * forward.y, 0.0f});
    }

    //43340F68/87: speed getter and its following property read precede the
    // whole capacity call, and survive mutations made by its weapon getters.
    const auto speed_physics = capture_movement_command_speed(player, bindings);
    const auto capacity = live_movement_acceleration_capacity(player, command, friction_velocity,
                                                              direction, bindings);
    const float current = dot(friction_velocity, direction);
    float available =
        minimum_number(speed_physics.player_speed_limit, speed_physics.player_max_speed) - current;
    if (0.0f >= available)
        available = 0.0f;
    else
        available = sse_min(available, capacity.amount);
    if (!(available > difference + 0.5f))
        return;

    float desired = difference + current;
    if (capacity.ducking &&
        field<float>(player, bindings.environment.fields.duck_amount_3309c0cf) == 1.0f)
        desired /= 0.34f;
    const float scale = desired / (command_length(command) + epsilon);
    command.forward_move *= scale;
    command.side_move *= scale;
    command.up_move *= scale;
}

} // namespace skeet
