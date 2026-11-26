#include "skeet/movement/simulation_capture.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float convar_float(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<float>(
        field<std::uint32_t>(object, 0x2C) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
}
MovementSimulationPlayer capture_movement_player(void* player,
                                                 const MovementCaptureOffsets& offsets) {
    MovementSimulationPlayer result;
    result.speed_scale = field<float>(player, offsets.speed_scale_7f5ed2d0);
    result.duck_speed = field<float>(player, offsets.duck_speed_fb7e3404);
    result.duck_amount = field<float>(player, offsets.duck_amount_3309c0cf);
    result.view_offset_z = field<float>(player, offsets.view_offset_c246315e + 8u);
    result.stamina = field<float>(player, offsets.stamina_2f33c681);
    result.hull_minimum = *engine::invoke<const MovementVector*>(player, 76);
    result.hull_maximum = *engine::invoke<const MovementVector*>(player, 77);
    result.ducking = field<std::uint8_t>(player, offsets.ducking_6b1a59b5);
    result.fall_velocity = field<float>(player, offsets.fall_velocity_7e9ba7bc);
    result.surface_friction = field<float>(player, offsets.field_f3c10e80 + 0x14u);
    result.movement_type = field<std::uint8_t>(player, offsets.movement_type_7501cb91);
    result.collision_mask = engine::invoke<std::uint32_t>(player, 152);
    result.field_f3c10e80_00 = field<std::uint8_t>(player, offsets.field_f3c10e80);
    return result;
}
MovementSimulationRules capture_movement_rules(const MovementCaptureConvars& convars,
                                               float interval) noexcept {
    MovementSimulationRules result;
    result.gravity = convar_float(convars.gravity);
    result.jump_impulse = convar_float(convars.jump_impulse);
    result.step_size = convar_float(convars.step_size);
    result.stamina_recovery = convar_float(convars.stamina_recovery);
    result.stamina_land = convar_float(convars.stamina_land);
    result.stamina_jump = convar_float(convars.stamina_jump);
    result.stamina_maximum = convar_float(convars.stamina_maximum);
    result.stop_speed = convar_float(convars.stop_speed);
    result.friction = convar_float(convars.friction);
    result.accelerate = convar_float(convars.accelerate);
    result.cvar_dceaeac8 = convar_float(convars.cvar_dceaeac8);
    result.interval_per_tick = interval;
    return result;
}
void initialize_live_movement_simulation(MovementSimulation& state, void* player,
                                         MovementVector position, MovementVector velocity,
                                         std::uint32_t player_flags, std::uint32_t mode,
                                         const MovementCaptureInput& input) {
    const auto rules = capture_movement_rules(input.convars, input.interval);
    const auto snapshot = capture_movement_player(player, input.offsets);
    initialize_movement_simulation(
        state, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)), position,
        velocity, player_flags, mode, snapshot, rules);
}
} // namespace skeet
