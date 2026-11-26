#include "skeet/engine/game_system_patch.hpp"

namespace skeet::engine {
// Devirtualized from VM entry 0x4351CF96; both null and nonnull paths verified.
bool capture_game_system_patch_target(void**& target_slot, void** candidate) noexcept {
    if (!candidate)
        return false;
    target_slot = candidate;
    return true;
}
}
