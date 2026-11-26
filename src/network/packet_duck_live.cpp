#include "skeet/network/packet_duck_live.hpp"
#include "skeet/network/command_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "packet_duck_body.hpp"
#include <cstring>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
struct LiveDuckInput {
    PacketCommandOwner& owner;
    void* player;

    std::uint8_t movement_type() const noexcept {
        return field<std::uint8_t>(player, owner.offsets.state_7501cb91);
    }
    std::uint8_t player_state() const noexcept {
        return field<std::uint8_t>(player, owner.offsets.state_ac7f2913);
    }
    std::uint8_t duck_state() const noexcept {
        return field<std::uint8_t>(player, owner.offsets.state_f3c10e80);
    }
    std::uint32_t player_flags() const noexcept {
        return field<std::uint32_t>(player, owner.offsets.player_flags_e2fbe695);
    }
    std::int32_t mode() const noexcept {
        return owner.globals.duck_mode;
    }
    std::int32_t lag_mode(const FakeLagState&) const noexcept {
        return owner.settings.amount.value;
    }
    bool active() const {
        return owner.settings.enabled.value &&
               hotkey_active(owner.settings.hotkey.value.binding, owner.globals.input);
    }
    float interval() const noexcept {
        return *owner.globals.interval;
    }
    Vector velocity() const noexcept {
        return field<Vector>(player, owner.offsets.velocity_36375c23);
    }
    const Vector& absolute_origin() const {
        return *engine::invoke<const Vector*>(player, 10);
    }
    std::int32_t engine_choked() const noexcept {
        return *owner.globals.choked_commands;
    }
};
}

void update_packet_duck_live(PacketCommandOwner& owner, void* player, std::uint32_t& buttons,
                             std::uint32_t& flags) {
    packet_duck_detail::update(owner.state, LiveDuckInput{owner, player}, buttons, flags);
}
} // namespace skeet::network
