#include "skeet/client/command_angle_retention.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
float attack_angle_difference(float current, float previous) noexcept {
    const float difference = current - previous;
    float result = static_cast<float>(std::fmod(static_cast<double>(difference), 360.0));
    if (current > previous) {
        if (result >= 180.0f)
            result -= 360.0f;
    } else if (result <= -180.0f)
        result += 360.0f;
    return result;
}
}

void retain_command_attack_angles(const CommandFrame& frame, Command& command, const void* player,
                                  const CommandMouseOffsets& offsets, const void* input) noexcept {
    if (!(frame.flags & 0x40U))
        return;
    if (field<std::uint32_t>(player, offsets.player_flags) & 8U)
        return;
    if (field<std::uint8_t>(player, offsets.player_state_043fa575) !=
        field<std::uint8_t>(player, offsets.player_state_334b460c))
        return;
    if (frame.weapon_id_d8 == 49)
        return;

    const auto buttons = command.movement.buttons;
    std::uint32_t retained_buttons;
    if (buttons & 1U)
        retained_buttons = buttons & ~1U;
    else {
        if (!(buttons & 0x800U) || frame.weapon_id_d8 != 64)
            return;
        retained_buttons = buttons & ~0x800U;
    }
    const auto previous_number = std::uint32_t(command.number) - 1U;
    const auto* commands = field<const Command*>(input, 0xF0);
    const auto& previous = commands[previous_number % 150U];
    if (std::bit_cast<std::uint32_t>(previous.number) != previous_number)
        return;

    const float pitch = attack_angle_difference(command.angles[0], previous.angles[0]);
    const float yaw = attack_angle_difference(command.angles[1], previous.angles[1]);
    const float yaw_square = yaw * yaw;
    const float pitch_square = pitch * pitch;
    const float distance = std::sqrt(pitch_square + yaw_square);
    const float maximum_step = frame.maximum_angle_step_1e8;
    if (!(distance >= maximum_step))
        return;
    const float scale = maximum_step / distance;
    const float scaled_yaw = scale * yaw;
    const float scaled_pitch = scale * pitch;
    command.angles[0] = scaled_pitch + previous.angles[0];
    const float limited_yaw = scaled_yaw + previous.angles[1];
    command.angles[2] = 0.0f;
    command.movement.buttons = retained_buttons;
    command.angles[1] = limited_yaw;
}

bool can_predict_command_copy(const Command& command, const void* variable) noexcept {
    if (variable) {
        for (;;) {
            const auto* parent = field<const void*>(variable, 0x1C);
            if (parent == variable)
                break;
            variable = parent;
        }
        const auto word = field<std::uint32_t>(variable, 0x30) ^
                          static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable));
        if (word & 0x80000000U)
            return false;
    }
    return std::bit_cast<std::uint32_t>(command.tick) != 0x7F7FFFFFU;
}

CommandCopyOverrideScope::CommandCopyOverrideScope(std::uint8_t* input) noexcept
    : target(input), saved(0) {
    if (target) {
        saved = *target;
        *target = 1;
    }
}
void CommandCopyOverrideScope::restore() noexcept {
    if (target) {
        *target = saved;
        target = nullptr;
    }
}

void prepare_prediction_command_copy(const CommandFrame& frame, const Command& command,
                                     Command& copy, std::uint8_t send_packet,
                                     const CommandButtonPolicy& policy,
                                     const CommandTrigonometry& trigonometry,
                                     const ScriptCommandEnvironment& environment) {
    const auto original_angles = frame.angles_118;
    const auto movement_type = frame.state_104;
    std::memcpy(&copy, &command, sizeof copy);
    reorient_command_movement(copy, original_angles, movement_type, true, send_packet == 0, policy,
                              trigonometry);
    sanitize_command(copy, (frame.flags & 8U) != 0, environment);
}
} // namespace skeet::client
