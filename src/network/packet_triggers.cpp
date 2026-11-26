#include "skeet/network/packet_control.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T out;
    std::memcpy(&out, static_cast<const std::byte*>(object) + offset, sizeof out);
    return out;
}
}
bool reload_trigger(const PacketWeaponBindings& bindings, void* weapon, std::uint32_t buttons) {
    if (!(buttons & 0x2000U))
        return false;
    const auto identifier = field<std::uint32_t>(weapon, bindings.reload_identifier);
    if (identifier == 0xffffffffU || identifier == engine::invoke<std::uint32_t>(weapon, 375))
        return false;
    return field<std::int32_t>(weapon, bindings.reload_counter) > 0;
}
bool secondary_attack_trigger(const PacketWeaponBindings& bindings, void* weapon,
                              std::uint32_t buttons, float time) {
    if (!(buttons & 0x800U) || !weapon || engine::invoke<std::int32_t>(weapon, 443) <= 0 ||
        engine::invoke<std::int32_t>(weapon, 462) != 0)
        return false;
    return time >= field<float>(weapon, bindings.next_secondary);
}
bool selected_weapon_trigger(const PacketWeaponBindings& bindings, std::uint8_t type,
                             std::uint32_t selected) {
    if (selected == 0)
        return false;
    if (type == 0 || type == 7 || type == 9)
        return true;
    if (selected & 0xffff0000U)
        return false;
    const auto entity = engine::invoke<void*, std::uint32_t>(bindings.entity_list, 3, selected);
    if (!entity)
        return false;
    const auto selected_type = engine::invoke<std::int32_t>(entity, 455);
    return selected_type == 8 || selected_type == 9;
}
bool classified_packet_weapon(std::uint16_t weapon,
                              std::span<const std::uint32_t, 3> classification) noexcept {
    return weapon < 65 && (classification[weapon >> 5] & (1U << (weapon & 31))) != 0;
}
void record_fake_lag_attack(FakeLagState& state, const PacketFrame& frame, std::int32_t weapon,
                            std::uint8_t type, std::uint32_t buttons, std::int32_t outgoing,
                            std::span<const std::uint32_t, 3> classification,
                            std::uint32_t& flags) noexcept {
    if (!classified_packet_weapon(std::uint16_t(weapon), classification))
        return;
    if ((frame.flags & 0x100000U) || ((frame.flags & 0x400U) && (buttons & 1U)) ||
        ((frame.flags & 2U) && (buttons & 0x800U) && (weapon == 0x40 || type == 0))) {
        state.value_7c = outgoing;
        flags = (flags & ~0x400U) | 0x200U;
    }
}
} // namespace skeet::network
