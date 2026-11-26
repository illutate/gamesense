#include "skeet/client/command_packet_angle_guard.hpp"
#include "skeet/client/command_pipeline.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/network/packet_control.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

bool firing_command(const CommandFrame& frame, const Command& command) noexcept {
    const auto flags = frame.flags;
    const auto buttons = command.movement.buttons;
    return (flags & 0x100000U) || ((flags & 0x400U) && (buttons & 1U)) ||
           ((flags & 2U) && (buttons & 0x800U) &&
            (frame.weapon_id_d8 == 64 || frame.weapon_kind_124 == 0));
}

// The inline limiter uses fmod followed by directional inclusive endpoint
// correction. At +180/-180 it differs from normalize_movement_angle.
float step_difference(float current, float reference) noexcept {
    const float difference = current - reference;
    float wrapped = static_cast<float>(std::fmod(static_cast<double>(difference), 360.0));
    if (current > reference) {
        if (!(wrapped < 180.0f))
            wrapped -= 360.0f;
    } else if (!(-180.0f < wrapped))
        wrapped += 360.0f;
    return wrapped;
}
}

void guard_changed_shot_packet(CommandFrame& frame, const Command& command,
                               std::uint8_t& send_packet, const void* weapon,
                               const CommandPacketAngleBindings& bindings) noexcept {
    if (frame.choked_15c < frame.choke_limit_fc && *bindings.enabled &&
        *bindings.firing_mode_enabled && !*bindings.send_mode && firing_command(frame, command) &&
        weapon &&
        network::classified_packet_weapon(field<std::uint16_t>(weapon, bindings.weapon_identifier),
                                          bindings.weapon_classification)) {
        const auto& previous = frame.previous_command.angles;
        if (command.angles[0] != previous[0] || command.angles[1] != previous[1] ||
            command.angles[2] != previous[2])
            send_packet = 0;
    }
    enforce_command_choke_limit(send_packet, frame.choked_15c, frame.choke_limit_fc);
}

void guard_packet_angles(CommandFrame& frame, Command& command, std::uint8_t& send_packet,
                         void* player, const void* weapon,
                         const CommandPacketAngleBindings& bindings) {
    enforce_command_choke_limit(send_packet, frame.choked_15c, frame.choke_limit_fc);
    if ((frame.flags & 0x400008U) == 0x400008U && frame.choked_15c < frame.choke_limit_fc &&
        send_packet == 0 && weapon && firing_command(frame, command))
        command.angles[0] = normalize_movement_angle(command.angles[0]) - 720.0f;

    if (!(frame.flags & 0x40U))
        return;
    const std::array<float, 3>* reference = &frame.previous_command.angles;
    if (!(field<std::uint32_t>(player, bindings.player_flags) & 8U) &&
        field<std::uint8_t>(player, bindings.player_first_byte) ==
            field<std::uint8_t>(player, bindings.player_second_byte)) {
        const auto previous_number =
            std::bit_cast<std::int32_t>(std::uint32_t(command.number) - 1U);
        const auto previous = engine::invoke<const Command*>(bindings.input, 8, 0, previous_number);
        if (previous)
            reference = &previous->angles;
    }

    const float pitch = step_difference(command.angles[0], (*reference)[0]);
    const float yaw = step_difference(command.angles[1], (*reference)[1]);
    const float yaw_square = yaw * yaw;
    const float pitch_square = pitch * pitch;
    const float distance = std::sqrt(pitch_square + yaw_square);
    const float maximum_step = field<float>(&frame, 0x1E8);
    if (!(distance >= maximum_step))
        return;
    const float scale = maximum_step / distance;
    const float scaled_yaw = scale * yaw;
    const float scaled_pitch = scale * pitch;
    command.angles[0] = scaled_pitch + (*reference)[0];
    const float limited_yaw = scaled_yaw + (*reference)[1];
    command.angles[2] = 0.0f;
    command.angles[1] = limited_yaw;
}
} // namespace skeet::client
