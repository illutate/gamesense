#include "skeet/menu/settings_menu_storage.hpp"
#include <utility>

namespace skeet::ui {

SettingsMenuStorage::SettingsMenuStorage(WindowEventState& window) noexcept
    : menu_key(defaults_.menu_key), menu_color(defaults_.menu_color),
      dpi_scale(defaults_.dpi_scale), lock_layout(window.window_size_locked),
      multithreading_config(defaults_.multithreading_config),
      multithreading(multithreading_config.value), maximum_unlag(defaults_.maximum_unlag),
      maximum_usercmd_ticks(defaults_.maximum_usercmd_ticks),
      clock_correction(defaults_.clock_correction), hold_aim(defaults_.hold_aim),
      fix_viewmodel_lag(defaults_.fix_viewmodel_lag), fix_networking(defaults_.fix_networking),
      fix_animations(defaults_.fix_animations), faster_grenade_toss(defaults_.faster_grenade_toss) {
}

SettingsMenuStorage::SettingsMenuStorage(std::shared_ptr<engine::NativeDataImage> image)
    : image_(std::move(image)),
      menu_key(
          engine::require_native_data_image(image_).native_cell<RegisteredHotkey>(0x43478DB8u)),
      menu_color(image_->native_cell<std::uint32_t>(0x43468FB0u)),
      dpi_scale(image_->native_cell<std::int32_t>(0x43475A94u)),
      lock_layout(image_->native_cell<WindowEventState>(0x43467E4Bu).window_size_locked),
      multithreading_config(image_->native_cell<BooleanConfig>(0x434683ECu)),
      multithreading(multithreading_config.value),
      maximum_unlag(image_->native_cell<std::int32_t>(0x43468230u)),
      maximum_usercmd_ticks(image_->native_cell<std::int32_t>(0x43469274u)),
      clock_correction(image_->native_cell<std::int32_t>(0x43468290u)),
      hold_aim(image_->native_cell<std::uint8_t>(0x43468D7Cu)),
      fix_viewmodel_lag(image_->native_cell<std::uint8_t>(0x4346A328u)),
      fix_networking(image_->native_cell<std::uint8_t>(0x43467CC0u)),
      fix_animations(image_->native_cell<std::uint8_t>(0x4346A527u)),
      faster_grenade_toss(image_->native_cell<std::uint8_t>(0x43467560u)) {}

void initialize_source_settings_menu_data(engine::NativeDataImage& data) {
    WindowEventState events;
    SettingsMenuStorage defaults(events);
    data.native_cell<RegisteredHotkey>(0x43478DB8u) = {};
    data.native_cell<std::uint32_t>(0x43468FB0u) = defaults.menu_color;
    data.native_cell<std::int32_t>(0x43475A94u) = defaults.dpi_scale;
    data.native_cell<WindowEventState>(0x43467E4Bu) = events;
    auto& descriptor = data.native_cell<BooleanConfig>(0x434683ECu);
    descriptor.next = nullptr;
    descriptor.previous = nullptr;
    descriptor.hash = defaults.multithreading_config.hash;
    descriptor.serialized_size = defaults.multithreading_config.serialized_size;
    descriptor.type = defaults.multithreading_config.type;
    descriptor.reserved = defaults.multithreading_config.reserved;
    descriptor.value = defaults.multithreading;
    data.native_cell<std::int32_t>(0x43468230u) = defaults.maximum_unlag;
    data.native_cell<std::int32_t>(0x43469274u) = defaults.maximum_usercmd_ticks;
    data.native_cell<std::int32_t>(0x43468290u) = defaults.clock_correction;
    data.native_cell<std::uint8_t>(0x43468D7Cu) = defaults.hold_aim;
    data.native_cell<std::uint8_t>(0x4346A328u) = defaults.fix_viewmodel_lag;
    data.native_cell<std::uint8_t>(0x43467CC0u) = defaults.fix_networking;
    data.native_cell<std::uint8_t>(0x4346A527u) = defaults.fix_animations;
    data.native_cell<std::uint8_t>(0x43467560u) = defaults.faster_grenade_toss;
}

SettingsMenuValues SettingsMenuStorage::values() noexcept {
    return {menu_key,
            menu_color,
            dpi_scale,
            lock_layout,
            multithreading,
            maximum_unlag,
            maximum_usercmd_ticks,
            clock_correction,
            hold_aim,
            fix_viewmodel_lag,
            fix_networking,
            fix_animations,
            faster_grenade_toss};
}

} //namespace skeet::ui
