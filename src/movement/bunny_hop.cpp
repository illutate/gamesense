#include "skeet/movement/bunny_hop.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
using namespace movement_detail;
bool BunnyHopGuard::releases_jump() const noexcept {
    if (const auto* semantic = std::get_if<const BunnyHopGuardNode*>(&source_)) {
        auto* node = *semantic;
        while (node->next != node)
            node = node->next;
        return node->release_marker == node;
    }
    const void* node = std::get<engine::NativeConVarReference>(source_).get();
    for (;;) {
        const void* parent;
        std::memcpy(&parent, static_cast<const std::byte*>(node) + 0x1C, sizeof(parent));
        if (parent == node)
            break;
        node = parent;
    }
    std::uint32_t encoded;
    std::memcpy(&encoded, static_cast<const std::byte*>(node) + 0x30, sizeof(encoded));
    return encoded == std::uint32_t(reinterpret_cast<std::uintptr_t>(node));
}
void suppress_airborne_jump(bool enabled, std::uint32_t flags, const BunnyHopGuard& guard,
                            UserCommand& command) noexcept {
    if (enabled && !(flags & on_ground) && guard.releases_jump())
        command.buttons &= ~jump_button;
}

// 0x43372CCB ground test, then 0x43372D3A..0x43372D68.
// The grounded edge at 0x43372CE3 belongs to a separate weapon/stop path.
void suppress_airborne_jump(bool enabled, std::uint32_t player_flags,
                            const BunnyHopGuardNode& guard, UserCommand& command) noexcept {
    if (!enabled || (player_flags & on_ground))
        return;
    const BunnyHopGuardNode* node = &guard;
    while (node->next != node)
        node = node->next;
    if (node->release_marker == node)
        command.buttons &= ~jump_button;
}

// Consumer 0x4337392E..0x43373A61. The intervening unrelated vertical-velocity
// update is performed by the shared solver, not by this command mutation.
void recover_bunny_hop_low_speed(bool enabled, const BunnyHopTraceResult& trace,
                                 MovementVector velocity, bool required_state, bool blocked,
                                 UserCommand& command, float forced_move) noexcept {
    if (trace.fraction < 1.0f || trace.flag_a || trace.flag_b || !required_state ||
        !(command.buttons & jump_button))
        return;
    const float squared_speed = velocity.y * velocity.y + velocity.x * velocity.x;
    if (!(4.0f > squared_speed) || blocked || !enabled)
        return;
    command.forward_move = forced_move;
}

// Consumer 0x433794E2..0x43379546, after the shared trace has completed.
void recover_bunny_hop_trace(bool enabled, const BunnyHopTraceResult& trace, bool blocked,
                             UserCommand& command, float forced_move) noexcept {
    if (trace.fraction < 1.0f || trace.flag_a || trace.flag_b || blocked || !enabled)
        return;
    command.forward_move = forced_move;
}

} // namespace skeet
