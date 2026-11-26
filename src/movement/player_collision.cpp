#include "skeet/client/player_physics_hooks.hpp"
#include "../client/player_physics_hooks_detail.hpp"

namespace skeet::client {
bool PlayerPhysicsHooks::should_collide(void* player, std::int32_t group, std::uint32_t mask) {
    if (group == 8) {
        const auto contents = engine::invoke<std::uint32_t>(player, 152) & 0x1800U;
        if (contents && (mask & 0x1800U) == contents)
            return false;
    }
    return player_physics_detail::original<bool>(player, 173, group, mask);
}
} //namespace skeet::client
