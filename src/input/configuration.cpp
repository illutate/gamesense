#include "skeet/input/runtime.hpp"

namespace skeet::input {
void initialize_menu_hotkey(ConfigRegistries& configuration,
                            RegisteredHotkey& menu_key) noexcept { // 434928A2
    initialize_hotkey(configuration.hotkeys, menu_key, 0x2e, HotkeyMode::hold);
}
}
