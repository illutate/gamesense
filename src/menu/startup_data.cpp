#include "skeet/menu/startup_data.hpp"
#include "skeet/menu/root_menu.hpp"
#include "skeet/config_storage/preserved.hpp"
#include <stdexcept>
#include <utility>

namespace skeet::ui {
namespace {
std::span<const std::byte> vector_bytes(const NativeConfigVector& vector) {
    if (!vector.begin) {
        if (vector.end || vector.capacity)
            throw std::logic_error("Menu baseline has no allocation for its published range");
        return {};
    }
    if (vector.end < vector.begin || vector.capacity < vector.end)
        throw std::logic_error("Menu baseline has an invalid native vector range");
    return {reinterpret_cast<const std::byte*>(std::uintptr_t(vector.begin)),
            std::size_t(vector.end - vector.begin)};
}
}

MenuStartupData::MenuStartupData(std::shared_ptr<engine::NativeDataImage> image)
    : image_(std::move(image)),
      dpi(engine::require_native_data_image(image_).native_cell<std::int32_t>(0x43475A94u)) {}

SettingsMenuValues MenuStartupData::settings_values() const {
    auto& data = *image_;
    return {data.native_cell<RegisteredHotkey>(0x43478DB8u),
            data.native_cell<std::uint32_t>(0x43468FB0u),
            data.native_cell<std::int32_t>(0x43475A94u),
            window_events().window_size_locked,
            data.native_cell<std::uint8_t>(0x434683FCu),
            data.native_cell<std::int32_t>(0x43468230u),
            data.native_cell<std::int32_t>(0x43469274u),
            data.native_cell<std::int32_t>(0x43468290u),
            data.native_cell<std::uint8_t>(0x43468D7Cu),
            data.native_cell<std::uint8_t>(0x4346A328u),
            data.native_cell<std::uint8_t>(0x43467CC0u),
            data.native_cell<std::uint8_t>(0x4346A527u),
            data.native_cell<std::uint8_t>(0x43467560u)};
}
ScriptsMenuValues MenuStartupData::script_values() const {
    auto& data = *image_;
    return {
        data.native_cell<std::uint8_t>(0x434675D4u), data.native_cell<std::uint8_t>(0x4346D544u),
        data.native_cell<std::uint8_t>(0x43475798u), data.native_cell<std::uint8_t>(0x43478C6Eu),
        data.native_cell<std::uint8_t>(0x4347CA28u)};
}
WindowEventState& MenuStartupData::window_events() const {
    static_assert(sizeof(WindowEventState) == 1);
    return image_->native_cell<WindowEventState>(0x43467E4Bu);
}
RootMenuGlobals& MenuStartupData::root_globals() const {
    static_assert(sizeof(RootMenuGlobals) == 4);
    return image_->native_cell<RootMenuGlobals>(0x4347CA3Cu);
}
std::span<const std::byte> MenuStartupData::baseline_a() const {
    return vector_bytes(image_->native_cell<NativeConfigVector>(0x43478CB8u));
}
std::span<const std::byte> MenuStartupData::baseline_b() const {
    return vector_bytes(image_->native_cell<NativeConfigVector>(0x43479770u));
}
} //namespace skeet::ui
