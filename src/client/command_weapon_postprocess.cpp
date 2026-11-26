#include "skeet/client/command_weapon_postprocess.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cstring>
#include <emmintrin.h>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float mouse_scale(const void* variable) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return std::bit_cast<float>(
        field<std::uint32_t>(variable, 0x2C) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable)));
}
std::int16_t mouse_count(float angle, float scale) noexcept {
    // CVTTSS2SI returns INT32_MIN for NaNs and overflow. Keeping AX gives
    // zero for that result, and preserves ordinary signed 16-bit wrapping.
    const auto count = _mm_cvttss_si32(_mm_div_ss(_mm_set_ss(angle), _mm_set_ss(scale)));
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(count));
}
}
void postprocess_command_after_phase7(const CommandWeaponPostprocessBindings& bindings,
                                      const CommandFrame& frame, void* player, Command& command,
                                      std::uint8_t send_packet) noexcept {
    // 4341999F reads tick as an integer word, not as a float timestamp.
    if (std::bit_cast<std::uint32_t>(command.tick) != 0x7F7FFFFFu && send_packet) {
        (*bindings.sent_angles)[0] = command.angles[0];
        (*bindings.sent_angles)[1] = command.angles[1];
        (*bindings.sent_angles)[2] = command.angles[2];
        (*bindings.sent_angles)[2] = 0.0f;
    }
    const auto& offsets = bindings.offsets;
    if (field<std::uint32_t>(player, offsets.player_flags) & 8u)
        return;
    if (field<std::uint8_t>(player, offsets.player_state_043fa575) !=
        field<std::uint8_t>(player, offsets.player_state_334b460c))
        return;
    if (command.angles[0] == frame.previous_command.angles[0] &&
        command.angles[1] == frame.previous_command.angles[1] &&
        command.angles[2] == frame.previous_command.angles[2])
        return;

    const auto number = std::uint32_t(command.number) - 1u;
    const auto* commands = field<const Command*>(bindings.input, 0xF0);
    const auto& previous = commands[number % 150u];
    if (std::bit_cast<std::uint32_t>(previous.number) != number)
        return;
    // Native copies all previous angles before it follows either ConVar's
    // parent chain. The ring entry can alias the mutable command.
    const auto previous_angles = previous.angles;
    const auto yaw_scale = mouse_scale(bindings.yaw_scale);
    if (yaw_scale != 0.0f)
        command.mouse_x = mouse_count(
            normalize_movement_angle(previous_angles[1] - command.angles[1]), yaw_scale);
    const auto pitch_scale = mouse_scale(bindings.pitch_scale);
    if (pitch_scale != 0.0f)
        command.mouse_y = mouse_count(
            normalize_movement_angle(command.angles[0] - previous_angles[0]), pitch_scale);
}
} // namespace skeet::client
