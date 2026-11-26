#include "skeet/movement/command_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cstring>

namespace skeet {
namespace {
using namespace movement_detail;
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
const void* terminal(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            return object;
        object = parent;
    }
}
bool self_integer(const void* object) noexcept {
    object = terminal(object);
    return field<std::uint32_t>(object, 0x30u) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}
}

float movement_command_convar_float(const void* object) noexcept {
    object = terminal(object);
    return std::bit_cast<float>(
        field<std::uint32_t>(object, 0x2Cu) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
std::uint32_t movement_command_player_flags(void* player,
                                            const MovementCommandBindings& b) noexcept {
    return field<std::uint32_t>(player, b.environment.fields.player_flags_e2fbe695);
}
std::uint8_t movement_command_player_type(void* player, const MovementCommandBindings& b) noexcept {
    return field<std::uint8_t>(player, b.fields.movement_type_7501cb91);
}
MovementVector movement_command_velocity(void* player, const MovementCommandBindings& b) noexcept {
    return field<MovementVector>(player, b.fields.velocity_36375c23);
}
MovementVector movement_command_position(void* player) {
    return *engine::invoke<const MovementVector*>(player, 0x28u / 4u);
}
const MovementVector& movement_command_position_ref(void* player) {
    return *engine::invoke<const MovementVector*>(player, 0x28u / 4u);
}
std::uint32_t movement_command_trace_mask(void* player) {
    return engine::invoke<std::uint32_t>(player, 0x260u / 4u);
}
PlayerAirGateBindings capture_player_air_gate_bindings(const MovementCommandBindings& b) noexcept {
    return {{b.fields.movement_type_7501cb91, b.fields.player_state_ac7f2913,
             b.fields.velocity_36375c23, b.fields.weapon_definition_5c9f0fbb,
             b.fields.weapon_blocked_b8969256, b.fields.player_next_attack_de530a8b,
             b.fields.weapon_next_attack_f71ce331},
            b.environment,
            &b.weapon_policy,
            &b.current_time,
            b.bunny_hop_guard};
}
bool movement_command_weapon_blocked(void* weapon, const MovementCommandBindings& b) noexcept {
    return field<std::uint8_t>(weapon, b.fields.weapon_blocked_b8969256) != 0;
}
float movement_command_player_attack(void* player, const MovementCommandBindings& b) noexcept {
    return field<float>(player, b.fields.player_next_attack_de530a8b);
}
float movement_command_weapon_attack(void* weapon, const MovementCommandBindings& b) noexcept {
    return field<float>(weapon, b.fields.weapon_next_attack_f71ce331);
}
float movement_command_attack_guard(void* player, const MovementCommandBindings& b) noexcept {
    return field<float>(player, b.fields.attack_guard_8815d240);
}
std::uint16_t movement_command_weapon_definition(void* weapon,
                                                 const MovementCommandBindings& b) noexcept {
    return field<std::uint16_t>(weapon, b.fields.weapon_definition_5c9f0fbb);
}
MovementCommandFriction
capture_movement_command_friction(const MovementCommandBindings& b) noexcept {
    MovementCommandFriction result;
    result.stop_speed = movement_command_convar_float(b.environment.convars.stop_speed);
    result.friction = movement_command_convar_float(b.environment.convars.friction);
    result.interval = *b.environment.interval;
    return result;
}

void process_live_duck_peek(MovementCommandOwner& owner, void* player, void* weapon,
                            UserCommand& command, std::uint8_t& send,
                            const MovementCommandBindings& b) {
    auto& state = owner.duck_peek;
    auto& flags = b.scheduler_flags;
    const DuckPeekSettings settings{b.duck_peek_hotkey, b.infinite_duck != 0};
    DuckPeekFrame frame{};
    frame.physics.player_flags = movement_command_player_flags(player, b);
    if (!(frame.physics.player_flags & on_ground) || (command.buttons & jump_button) ||
        (!hotkey_active(settings.hotkey, b.input) && !(flags & 0x100u))) {
        process_duck_peek(settings, b.input, state, frame, command, flags, send);
        return;
    }
    if (!settings.crouch_cycle) {
        const auto velocity = movement_command_velocity(player, b);
        const float squared =
            (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
        if (!(squared <= 10.0f))
            return;
        command.buttons |= duck_button;
        if (weapon) {
            const float elapsed =
                b.current_time - field<float>(weapon, b.fields.weapon_simulation_time_0176a381);
            const float weapon_delay = engine::invoke<float>(weapon, 0x6C4u / 4u);
            const float cvar_delay = movement_command_convar_float(b.duck_delay);
            if (elapsed > maximum_number(cvar_delay, weapon_delay) + 0.75f)
                command.buttons &= ~duck_button;
        }
        return;
    }
    const bool separate = (state.original_buttons & duck_button) && state.last_key != 27 &&
                          std::uint32_t(state.last_key) != (settings.hotkey.packed >> 2);
    if (separate) {
        if ((!(state.previous_buttons & duck_button) || state.release_latch) && state.choked < 14)
            frame.physics.duck_amount =
                field<float>(player, b.environment.fields.duck_amount_3309c0cf);
    } else {
        frame.duck_delay = movement_command_convar_float(b.duck_delay);
        frame.player_duck_time = field<float>(player, b.fields.duck_time_04b798c6);
        frame.current_time = b.current_time;
        if (!(frame.player_duck_time + frame.duck_delay > frame.current_time)) {
            frame.physics.duck_speed =
                field<float>(player, b.environment.fields.duck_speed_fb7e3404);
            if (!(6.0f > frame.physics.duck_speed)) {
                frame.command_budget = client::query_command_budget(b.budget);
                const auto budget =
                    std::bit_cast<std::int32_t>(std::uint32_t(frame.command_budget) - 1u);
                if ((budget < *state.configured_limit ? budget : *state.configured_limit) >= 3) {
                    frame.physics.interval_per_tick = *b.environment.interval;
                    frame.physics.duck_amount =
                        field<float>(player, b.environment.fields.duck_amount_3309c0cf);
                }
            }
        }
    }
    process_duck_peek(settings, b.input, state, frame, command, flags, send);
}

MovementCommandHull capture_quick_peek_origin_hull(void* player) {
    MovementCommandHull result;
    result.mask = engine::invoke<std::uint32_t>(player, 0x260u / 4u);
    auto* collideable = engine::invoke<void*>(player, 0xCu / 4u);
    const auto* maximum = engine::invoke<const MovementVector*>(collideable, 2);
    collideable = engine::invoke<void*>(player, 0xCu / 4u);
    const auto* minimum = engine::invoke<const MovementVector*>(collideable, 1);
    result.maximum = *maximum;
    result.minimum = *minimum;
    return result;
}
MovementCommandHull capture_quick_peek_limit_hull(void* player) {
    MovementCommandHull result;
    const auto* minimum = engine::invoke<const MovementVector*>(player, 0x130u / 4u);
    const auto* maximum = engine::invoke<const MovementVector*>(player, 0x134u / 4u);
    result.minimum = *minimum;
    result.maximum = *maximum;
    result.mask = engine::invoke<std::uint32_t>(player, 0x260u / 4u);
    return result;
}
QuickPeekHistory capture_quick_peek_history(void* player, std::int32_t number,
                                            const MovementCommandBindings& b) {
    QuickPeekHistory result;
    result.command_number = number;
    result.position = movement_command_position(player);
    result.unavailable = b.history_command != -1 && b.history_available != 0;
    result.grounded = (movement_command_player_flags(player, b) & on_ground) != 0;
    return result;
}
bool movement_command_quick_peek_shot(const QuickPeekState& state, void* player,
                                      const MovementCommandBindings& b) noexcept {
    std::int32_t previous = -1;
    for (int i = 0; i < 16; ++i) {
        const auto number =
            std::bit_cast<std::int32_t>(std::uint32_t(b.last_acknowledged) - std::uint32_t(i));
        const auto* record =
            prediction::CommandHistoryView::from_owner(b.history_owner).find(number);
        if (!record)
            break;
        if ((record->flags & 1u) && record->shot_time != 0.0f && record->fired) {
            previous = number;
            break;
        }
    }
    if (previous != -1) {
        for (int i = 1; i < 16; ++i) {
            const auto number =
                std::bit_cast<std::int32_t>(std::uint32_t(previous) + std::uint32_t(i));
            if (number > b.last_acknowledged)
                break;
            const auto* record =
                prediction::CommandHistoryView::from_owner(b.history_owner).find(number);
            if (!record)
                break;
            if ((record->flags & 1u) && record->shot_time == 0.0f && !record->fired)
                return true;
        }
    }
    return field<std::int32_t>(player, b.fields.shot_counter_8cfc633a) > state.shot_counter;
}
MovementPhysics capture_movement_command_speed(void* player, const MovementCommandBindings& b) {
    return capture_movement_command_physics(player, b.environment);
}

GroundAcceleration movement_command_ground_acceleration(void* player, const UserCommand& command,
                                                        MovementVector velocity,
                                                        MovementVector direction, float wish_speed,
                                                        const MovementCommandBindings& b) {
    MovementPhysics physics{};
    physics.accelerate = movement_command_convar_float(b.environment.convars.accelerate);
    if (0.0f >= wish_speed - dot(velocity, direction))
        return {0.0f, 250.0f, physics.accelerate};
    UserCommand copied{};
    copied.buttons = command.buttons;
    if (!(copied.buttons & duck_button)) {
        physics.ducking = field<std::uint8_t>(player, b.environment.fields.ducking_6b1a59b5) != 0;
        if (!physics.ducking)
            physics.player_flags = movement_command_player_flags(player, b);
    }
    physics.weapon_speed_enabled = !self_integer(b.environment.convars.accelerate_weapon_speed);
    if (physics.weapon_speed_enabled) {
        const auto handle =
            field<std::int32_t>(player, b.environment.fields.weapon_handle_cd812654);
        if (handle != -1) {
            auto* weapon = engine::invoke<void*>(b.environment.entity_list, 4, handle);
            if (weapon) {
                physics.has_weapon = true;
                physics.weapon_max_speed = engine::invoke<float>(weapon, 0x6E8u / 4u);
                physics.weapon_mode_count = engine::invoke<std::int32_t>(weapon, 0x738u / 4u);
                if (physics.weapon_mode_count > 0)
                    physics.weapon_mode = engine::invoke<std::int32_t>(weapon, 0x6ECu / 4u);
            }
        }
    }
    physics.interval_per_tick = *b.environment.interval;
    return ground_acceleration(physics, copied, velocity, direction, wish_speed);
}
bool write_live_movement_counter(void* player, UserCommand& command, MovementVector velocity,
                                 MovementAngleView angles, const MovementCommandBindings& b) {
    command.buttons &= ~0x61au;
    command.forward_move = 0.0f;
    command.side_move = 0.0f;
    const float speed = horizontal_length(velocity);
    if (!(speed >= 0.1f))
        return false;
    const auto friction = capture_movement_command_friction(b);
    const float remaining =
        maximum_number(speed - (maximum_number(friction.stop_speed, speed) * friction.friction) *
                                   friction.interval,
                       0.0f);
    if (!(remaining >= 0.1f))
        return false;
    const float factor = remaining / speed;
    const MovementVector decayed{velocity.x * factor, velocity.y * factor, factor * 0.0f};
    const MovementVector reverse{negate(velocity.x), negate(velocity.y), 0.0f};
    angles = horizontal_angles(reverse, false);
    command.forward_move = 450.0f;
    const auto physics = capture_movement_command_speed(player, b);
    const float maximum = minimum_number(physics.player_speed_limit, physics.player_max_speed);
    const auto acceleration = movement_command_ground_acceleration(
        player, command, decayed, normalize_horizontal(reverse), maximum, b);
    if (acceleration.amount > remaining)
        command.forward_move = 0.001f;
    return true;
}
} // namespace skeet
