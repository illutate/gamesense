#include "skeet/movement/rage_stop.hpp"
#include "skeet/movement/acceleration.hpp"
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

const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto* const parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            return object;
        object = parent;
    }
}

float convar_float(const void* object) noexcept {
    object = convar_parent(object);
    return std::bit_cast<float>(
        field<std::uint32_t>(object, 0x2Cu) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}

bool convar_self_integer(const void* object) noexcept {
    object = convar_parent(object);
    return field<std::uint32_t>(object, 0x30u) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}

// Live433AC9C0. The acceleration cvar is read even when additional speed is
// nonpositive. Player/weapon reads follow that gate; interval follows all
// engine calls. The command button word is retained across those calls.
float counter_acceleration(void* player, UserCommand& command, MovementVector velocity,
                           MovementVector direction, float wish_speed,
                           const MovementEnvironmentBindings& bindings) {
    MovementPhysics physics{};
    physics.accelerate = convar_float(bindings.convars.accelerate);
    const float additional = wish_speed - dot(velocity, direction);
    if (0.0f >= additional)
        return 0.0f;

    UserCommand acceleration_command{};
    acceleration_command.buttons = command.buttons;
    if (!(acceleration_command.buttons & duck_button)) {
        physics.ducking = field<std::uint8_t>(player, bindings.fields.ducking_6b1a59b5) != 0;
        if (!physics.ducking)
            physics.player_flags =
                field<std::uint32_t>(player, bindings.fields.player_flags_e2fbe695);
    }
    physics.weapon_speed_enabled = !convar_self_integer(bindings.convars.accelerate_weapon_speed);
    if (physics.weapon_speed_enabled) {
        const auto handle = field<std::int32_t>(player, bindings.fields.weapon_handle_cd812654);
        if (handle != -1) {
            auto* const weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
            if (weapon) {
                physics.has_weapon = true;
                physics.weapon_max_speed = engine::invoke<float>(weapon, 0x6E8u / 4u);
                physics.weapon_mode_count = engine::invoke<std::int32_t>(weapon, 0x738u / 4u);
                if (physics.weapon_mode_count > 0)
                    physics.weapon_mode = engine::invoke<std::int32_t>(weapon, 0x6ECu / 4u);
            }
        }
    }
    physics.interval_per_tick = *bindings.interval;
    return ground_acceleration(physics, acceleration_command, velocity, direction, wish_speed)
        .amount;
}

// Live43388405. Friction/player/weapon getters are intentionally below the
// same speed gates as the original. This cannot use one entry-time snapshot.
void write_rage_counter_movement(void* player, client::Command& command, MovementVector velocity,
                                 const MovementEnvironmentBindings& bindings) {
    auto& movement = command.movement;
    movement.buttons &= ~0x61au;
    movement.forward_move = 0.0f;
    movement.side_move = 0.0f;
    const float speed = horizontal_length(velocity);
    if (!(speed >= 0.1f))
        return;

    const float stop_speed = convar_float(bindings.convars.stop_speed);
    const float friction = convar_float(bindings.convars.friction);
    const float control = maximum_number(stop_speed, speed);
    const float drop = (control * friction) * (*bindings.interval);
    const float after_friction = maximum_number(speed - drop, 0.0f);
    if (!(after_friction >= 0.1f))
        return;
    const float fraction = after_friction / speed;
    const MovementVector friction_velocity{velocity.x * fraction, velocity.y * fraction,
                                           fraction * 0.0f};
    const MovementVector reverse{negate(velocity.x), negate(velocity.y), 0.0f};
    const auto angles = horizontal_angles(reverse, false);
    command.angles[1] = angles.yaw;
    command.angles[0] = angles.pitch;
    command.angles[2] = 0.0f;
    movement.forward_move = 450.0f;
    const auto direction = normalize_horizontal(reverse);

    const auto command_physics = capture_movement_command_physics(player, bindings);
    const float maximum =
        minimum_number(command_physics.player_speed_limit, command_physics.player_max_speed);
    const float acceleration =
        counter_acceleration(player, movement, friction_velocity, direction, maximum, bindings);
    if (acceleration > after_friction)
        movement.forward_move = 0.001f;
}
}

float rage_stop_speed(void* weapon) {
    float maximum = engine::invoke<float>(weapon, 0x6E8u / 4u);
    if (maximum == 0.0f)
        maximum = 250.0f;
    return maximum * 0.34f - 1.0f;
}

float rage_move_speed(void* player, const RageStopBindings& bindings) {
    const auto physics = capture_movement_command_physics(player, bindings.environment);
    return minimum_number(physics.player_speed_limit, physics.player_max_speed) * 0.34f - 1.0f;
}

void prepare_rage_stop_command(const client::CommandFrame&, void* player, client::Command& command,
                               const MovementSimulation& simulation,
                               const RageStopBindings& bindings) {
    if (simulation.player_flags & on_ground)
        write_rage_counter_movement(player, command, simulation.velocity, bindings.environment);
}

} // namespace skeet
