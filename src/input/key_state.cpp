#include "skeet/input/runtime.hpp"
#include <stdexcept>

namespace skeet::input {
bool EngineInterfaces::in_game() const {
    return engine::invoke<bool>(engine_client, 26);
}
bool EngineInterfaces::console_visible() const {
    return engine::invoke<bool>(engine_client, 11);
}
bool EngineInterfaces::chat_active() const { // 0x43384AE8
    if (!find_hud_element)
        return false;
    auto* chat = find_hud_element(hud, "CCSGO_HudChat");
    return chat && engine::invoke<bool>(chat, 13);
}
bool is_typing_key(std::uint32_t key) noexcept { // 0x4334C872
    return key - 0x41 < 26 || key - 0x30 < 10 || key == 0x20 || key == 8 || key == 13 || key == 16;
}
void Runtime::set_key(std::uint32_t key, bool pressed) {
    // RtlSetBit/RtlClearBit ignore the bitmap's declared size. A valid Win32
    // virtual-key value is still required to fit the actual 256-bit storage.
    if (key >= keys.size() * 32)
        throw std::out_of_range("Virtual key exceeds input bitmap storage");
    const auto mask = std::uint32_t(1) << (key & 31);
    if (pressed)
        keys[key >> 5] |= mask;
    else
        keys[key >> 5] &= ~mask;
}
void Runtime::update_keys(const ui::InputMessage& message, bool consumed) { // 0x433B49FE
    if (message.message - 0x200 < 15) {
        mouse_buttons = static_cast<std::uint16_t>(message.parameter);
        std::uint32_t suppressed = 0;
        if (!consumed && interfaces.in_game() &&
            (interfaces.chat_active() || interfaces.console_visible()))
            suppressed = 7;
        constexpr std::uint8_t key_map[]{1, 2, 16, 17, 4, 5, 6};
        for (std::uint32_t i = 0; i < std::size(key_map); ++i) {
            const auto mask = std::uint32_t(1) << i;
            if (!(message.parameter & mask))
                set_key(key_map[i], false);
            else if (!(suppressed & mask))
                set_key(key_map[i], true);
        }
    } else if (message.message == 0x100) {
        if (is_typing_key(message.parameter) && interfaces.in_game() &&
            (interfaces.chat_active() || interfaces.console_visible()))
            return;
        if (message.parameter != 0 && message.parameter != 0x1b)
            set_key(message.parameter, true);
    } else if (message.message == 0x101)
        set_key(message.parameter & 0xff, false);
}
}
