#include "skeet/client/local_player_animation_hooks.hpp"
#include "skeet/movement/steering_math.hpp"
#include <cmath>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
void** original(void* player) noexcept {
    const auto* metadata =
        static_cast<const engine::VtableHookMetadata*>((*static_cast<void***>(player))[-2]);
    return metadata->original_table;
}
}
std::uint32_t LocalPlayerAnimationHooks::update_strafe(void* player, std::uint32_t first,
                                                       std::uint32_t second,
                                                       std::uintptr_t caller) {
    const auto& b = bindings;
    const auto& o = b.offsets;
    if (b.animation_update && caller == b.strafe_call_site) {
        const auto* command = field<const void*>(player, o.current_command_f58e6118);
        if (command) {
            const auto* state = field<const void*>(player, o.animation_state_3d52a1a7);
            if (state && b.command_owner) {
                const auto buttons = field<std::uint32_t>(command, 0x30);
                MovementVector forward, right;
                movement_angle_vectors({0.0f, field<float>(state, 0x80), 0.0f}, &forward, &right,
                                       nullptr);
                const auto length =
                    std::sqrt((right.x * right.x + right.y * right.y) + right.z * right.z);
                const auto inverse = 1.0f / (length + 0.00000011920928955078125f);
                right = {right.x * inverse, right.y * inverse, right.z * inverse};
                const auto velocity = field<MovementVector>(state, 0xE0);
                const auto forward_motion =
                    (velocity.x * forward.x + velocity.y * forward.y) + velocity.z * forward.z;
                const auto side_motion =
                    (velocity.x * right.x + velocity.y * right.y) + velocity.z * right.z;
                const auto fraction = field<float>(state, 0xF8);
                bool strafing = false;
                if (fraction >= 0.730000019073486328125f) {
                    if (buttons & 0x400U) {
                        if (!(buttons & 0x200U))
                            strafing = -0.62999999523162841796875f > side_motion;
                    } else if (buttons & 0x200U)
                        strafing = side_motion > 0.62999999523162841796875f;
                }
                if (fraction >= 0.64999997615814208984375f) {
                    if (buttons & 8U) {
                        if (!(buttons & 0x10U))
                            strafing |= -0.550000011920928955078125f > forward_motion;
                    } else if (buttons & 0x10U)
                        strafing |= forward_motion > 0.550000011920928955078125f;
                }
                const auto value = std::uint8_t(strafing);
                std::memcpy(static_cast<std::byte*>(player) + o.strafe_00a5bd68, &value,
                            sizeof value);
            }
        }
    }
    return reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uint32_t, std::uint32_t>>(
        original(player)[222])(player, first, second);
}
} // namespace skeet::client
