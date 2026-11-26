#include "skeet/client/command_packet_transition.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T read_field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

void decrement_word(void* object, std::ptrdiff_t offset) noexcept {
    const auto value = read_field<std::uint32_t>(object, offset) - 1U;
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}

// Exact 433240A6: both holder levels and the encoded guard offset are read
// after engine slot 26, so mutations made by that query remain observable.
bool player_blocks_transition(const CommandPacketTransitionBindings& bindings) noexcept {
    const auto* holder = bindings.shared_player;
    if (!holder || !*holder)
        return true;
    const auto offset = std::bit_cast<std::int32_t>(*bindings.player_guard_word ^
                                                    bindings.player_guard_key ^ 0xBF567619U);
    return read_field<std::uint8_t>(*holder, offset) != 0;
}
}

void apply_command_packet_transition(std::uint8_t requested_packet,
                                     std::uint8_t& engine_send_packet,
                                     const std::uint8_t& packet_context,
                                     const CommandPacketTransitionBindings& bindings) {
    if (requested_packet == engine_send_packet)
        return;

    if (requested_packet == 0 && engine_send_packet != 0 && packet_context != 0 &&
        bindings.enabled != 0 && bindings.transition_enabled != 0 &&
        engine::invoke<std::uint8_t>(bindings.engine_client, 26) != 0 &&
        !player_blocks_transition(bindings)) {
        void* const channel = engine::invoke<void*>(bindings.engine_client, 78);
        if (channel) {
            // Native IDIV uses the signed sequence and the retained divisor
            // 150 from stack +30. Each record is the startup reset's 8 bytes.
            const auto sequence = read_field<std::int32_t>(channel, 0x18);
            const auto outgoing = *bindings.last_outgoing;
            auto& record = bindings.acknowledgements[sequence % 150];
            record[0] = std::bit_cast<std::uint32_t>(sequence);
            record[1] = outgoing;

            engine::invoke<std::int32_t>(channel, 46, static_cast<void*>(nullptr));
            decrement_word(channel, 0x2C);
            decrement_word(channel, 0x18);
        }
    }
    engine_send_packet = requested_packet;
}

} // namespace skeet::client
