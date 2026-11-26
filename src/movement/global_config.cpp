#include "skeet/movement/global_config.hpp"

namespace skeet {
void initialize_infinite_duck_config(ConfigRegistries& registries,
                                     BooleanConfig& setting) noexcept {
    append_config_link(registries.descriptors, setting);
}
void initialize_air_duck_config(ConfigRegistries& registries, IntegerConfig& setting) noexcept {
    append_config_link(registries.descriptors, setting);
    setting.value = 0;
}
void initialize_edge_jump_hotkey(ConfigRegistries& registries, HotkeyConfig& setting) noexcept {
    append_config_link(registries.descriptors, setting);
    auto& hotkey = setting.value;
    append_hotkey(registries.hotkeys, hotkey);
    hotkey.callback_object = nullptr;
    hotkey.callback = nullptr;
    hotkey.binding.packed = (hotkey.binding.packed & 1u) | 0x18u;
    hotkey.binding.mode = HotkeyMode::hold;
}
} //namespace skeet
