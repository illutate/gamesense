#include "skeet/movement/simulation_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

namespace skeet {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1Cu);
        if (parent == object)
            return object;
        object = parent;
    }
}
bool convar_self_integer(const void* object) noexcept {
    object = convar_parent(object);
    return field<std::uint32_t>(object, 0x30u) ==
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}
}

MovementPhysics capture_movement_command_physics(void* player,
                                                 const MovementEnvironmentBindings& bindings) {
    MovementPhysics result{};
    result.player_max_speed = engine::invoke<float>(player, 0x450u / 4u);
    result.player_speed_limit = field<float>(player, bindings.fields.speed_limit_3eb71843);
    return result;
}

MovementGroundWeaponPhysics
capture_movement_ground_weapon_physics(void* player, const UserCommand& command,
                                       std::uint32_t simulation_player_flags,
                                       const MovementEnvironmentBindings& bindings) {
    MovementGroundWeaponPhysics result{};
    auto& physics = result.physics;
    physics.player_flags = simulation_player_flags;
    if (!(command.buttons & duck_button))
        physics.ducking = field<std::uint8_t>(player, bindings.fields.ducking_6b1a59b5) != 0;
    result.duck =
        (command.buttons & duck_button) || physics.ducking || (simulation_player_flags & 6u);
    result.walk = (command.buttons & speed_button) && !result.duck;
    physics.weapon_speed_enabled = !convar_self_integer(bindings.convars.accelerate_weapon_speed);
    if (!physics.weapon_speed_enabled)
        return result;
    const auto handle = field<std::int32_t>(player, bindings.fields.weapon_handle_cd812654);
    if (handle == -1)
        return result;
    auto* const weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
    if (!weapon)
        return result;
    physics.has_weapon = true;
    physics.weapon_max_speed = engine::invoke<float>(weapon, 0x6E8u / 4u);
    physics.weapon_mode_count = engine::invoke<std::int32_t>(weapon, 0x738u / 4u);
    if (physics.weapon_mode_count > 0)
        physics.weapon_mode = engine::invoke<std::int32_t>(weapon, 0x6ECu / 4u);
    return result;
}

bool movement_player_duck_slowdown(void* player,
                                   const MovementEnvironmentBindings& bindings) noexcept {
    return field<std::uint8_t>(player, bindings.fields.field_a15c3105) != 0;
}
bool movement_jump_speed_cap(const MovementEnvironmentBindings& bindings) noexcept {
    return convar_self_integer(bindings.convars.jump_speed_cap);
}
bool movement_landing_stamina_present(const MovementEnvironmentBindings& bindings) noexcept {
    return bindings.convars.stamina_land != nullptr;
}

} // namespace skeet
