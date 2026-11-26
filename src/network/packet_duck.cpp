#include "skeet/network/packet_control.hpp"
#include "packet_duck_body.hpp"

namespace skeet::network {
namespace {
struct CapturedDuckInput {
    const DuckPacketInput& input;
    std::uint8_t movement_type() const noexcept {
        return input.player_state_7501cb91;
    }
    std::uint8_t player_state() const noexcept {
        return input.player_state_ac7f2913;
    }
    std::uint8_t duck_state() const noexcept {
        return input.player_state_f3c10e80;
    }
    std::uint32_t player_flags() const noexcept {
        return input.player_flags;
    }
    std::int32_t mode() const noexcept {
        return input.mode;
    }
    std::int32_t lag_mode(const FakeLagState& state) const noexcept {
        return state.mode;
    }
    bool active() const {
        return fake_lag_active(input.activation);
    }
    float interval() const noexcept {
        return input.interval;
    }
    Vector velocity() const noexcept {
        return input.velocity;
    }
    const Vector& absolute_origin() const noexcept {
        return input.absolute_origin;
    }
    std::int32_t engine_choked() const noexcept {
        return input.engine_choked;
    }
};
}
bool fake_lag_active(const FakeLagActivation& activation) {
    return activation.enabled && hotkey_active(activation.key, activation.input);
}
void update_packet_duck(FakeLagState& state, const DuckPacketInput& input, std::uint32_t& buttons,
                        std::uint32_t& flags) {
    packet_duck_detail::update(state, CapturedDuckInput{input}, buttons, flags);
}
void process_fake_lag_command(FakeLagState& state, const PacketCommandContext& context,
                              const DuckPacketInput& input, std::uint32_t& buttons,
                              std::uint32_t& flags) {
    prepare_fake_lag_command(state, context, flags);
    update_packet_duck(state, input, buttons, flags);
}
} // namespace skeet::network
