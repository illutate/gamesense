#include "skeet/client/player_physics_hooks.hpp"
#include "../client/player_physics_hooks_detail.hpp"

namespace skeet::client {
void PlayerPhysicsHooks::update_interpolated_state(void* player,
                                                   std::array<std::uint32_t, 4> arguments) {
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    const auto& cache = bindings.players.get(index)->native;
    if (player_physics_detail::field<std::uint32_t>(&cache, 0x1D4) == 0)
        return;
    player_physics_detail::original<void>(player, 78, arguments[0], arguments[1], arguments[2],
                                          arguments[3]);
}
std::uint8_t PlayerPhysicsHooks::interpolate(void* player, std::uint32_t time_bits) {
    if (auto* guard = bindings.interpolation_guard_43468fa4)
        *guard = 0;
    // On x86 this is the native float argument's unchanged four-byte stack
    // representation. No x87 load/store may quiet a signaling NaN here.
    return player_physics_detail::original<std::uint8_t>(player, 113, time_bits);
}
} //namespace skeet::client
