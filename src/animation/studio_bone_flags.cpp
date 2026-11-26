#include "skeet/client/player_physics_hooks.hpp"
#include "../client/player_physics_hooks_detail.hpp"

namespace skeet::client {
void* PlayerPhysicsHooks::studio_header(void* player) {
    using namespace player_physics_detail;
    auto* header = original<void*>(player, 109);
    if (!header)
        return header;
    const auto* studio = field<const void*>(header, 0);
    const auto count = field<std::int32_t>(studio, 0x9C);
    for (std::int32_t index = 0; index < count; ++index) {
        auto* flags = field<std::uint32_t*>(header, 0x30);
        flags[index] &= ~4U;
    }
    return header;
}
} //namespace skeet::client
