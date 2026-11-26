#include "skeet/client/command_pipeline.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/network/packet_control.hpp"
#include <bit>

namespace skeet::client {
void* register_command_phase_owner(std::vector<void*>& owners, void* owner) {
    owners.push_back(
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(owner)) == 0xFFFFFFFCu ? nullptr
                                                                                           : owner);
    return owner;
}
void initialize_command_frame(CommandFrame& frame) noexcept {
    frame.weapon_address = frame.weapon_data_address = 0;
    frame.previous_command = {};
    frame.field_1ec = frame.field_1f0 = 0;
}
void prepare_command_frame(CommandFrame& frame, const Command& saved, std::uint32_t send,
                           std::int32_t choked, std::int32_t number) noexcept {
    frame.send_packet_address = send;
    frame.weapon_address = frame.weapon_data_address = 0;
    frame.previous_command = saved;
    frame.choked_15c = choked;
    frame.field_18c = frame.field_1a0 = frame.field_1d4 = frame.field_1dc = frame.field_1ec =
        frame.field_1f0 = 0;
    frame.command_number_ac = number;
}
void advance_command_cycle_flags(std::uint32_t& flags, std::int32_t choked) noexcept {
    if (choked != 0)
        return;
    const auto state = (flags >> 9) & 3u;
    if (state)
        flags ^= (((state - 1u) << 9) ^ flags) & 0x600u;
}
void enforce_command_choke_limit(std::uint8_t& send_packet, std::int32_t choked,
                                 std::int32_t limit) noexcept {
    if (!send_packet && choked >= limit)
        send_packet = 1;
}
void sanitize_final_command_angles(Command& command) noexcept {
    for (float angle : command.angles)
        if ((std::bit_cast<std::uint32_t>(angle) & 0x7F800000u) == 0x7F800000u) {
            command.angles = {0.0f, 0.0f, 0.0f};
            return;
        }
}
void record_command_shot(std::uint32_t flags, const Command& command, std::int32_t last_outgoing,
                         std::int32_t& last_shot) noexcept {
    if (((flags & 0x400u) && (command.movement.buttons & 1u)) ||
        ((flags & 2u) && (command.movement.buttons & 0x800u)))
        last_shot = last_outgoing;
}
void record_sent_command_history(SentCommandHistory& history, const Command& command,
                                 std::uint8_t send_packet,
                                 const SentCommandTiming& timing) noexcept {
    record_sent_command_history(SentCommandHistoryView(history), command, send_packet, timing);
}
void record_sent_command_history(SentCommandHistoryView history, const Command& command,
                                 std::uint8_t send_packet,
                                 const SentCommandTiming& timing) noexcept {
    if (!send_packet)
        return;
    history.angles = command.angles;
    for (float& angle : history.angles)
        angle = normalize_movement_angle(angle);
    const auto first = std::bit_cast<std::int32_t>(std::uint32_t(timing.last_outgoing) + 1u);
    if (command.number < first || std::uint32_t(command.number) - std::uint32_t(first) >= 150u)
        return;
    const auto tick = network::adjusted_command_tick(
        timing.base_tick, timing.have_ack, timing.acknowledged, timing.have_sent, timing.sent);
    for (auto number = first;; number = std::bit_cast<std::int32_t>(std::uint32_t(number) + 1u)) {
        history.records[unsigned(number % 150)] = {number, tick};
        if (number == command.number)
            break;
    }
}
} // namespace skeet::client
