#include "skeet/config_storage/preserved.hpp"
#include "skeet/config_storage/settings_owner.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/menu/startup_window_name.hpp"
#include <stdexcept>
#include <utility>

namespace skeet {
namespace {
std::byte* constructed_settings(const engine::NativeDataImage& image) {
    const auto address = image.native_cell<std::uint32_t>(0x43478C68);
    if (!address)
        throw std::logic_error("CONFIG settings owner has not been initialized");
    return reinterpret_cast<std::byte*>(std::uintptr_t(address));
}
engine::UnloadSystemFunctions terminal_memory(const engine::JobMemory& memory) {
    if (!memory.free || !memory.process_heap)
        throw std::invalid_argument("Native CONFIG finalizers require their original process heap");
    engine::UnloadSystemFunctions result;
    result.free_heap = memory.free;
    result.process_heap = memory.process_heap;
    return result;
}
}
PreservedConfigStorage::PreservedConfigStorage(engine::PreservedInitialization& initialization)
    : PreservedConfigStorage(initialization.image_owner()) {}
PreservedConfigStorage::PreservedConfigStorage(std::shared_ptr<engine::NativeDataImage> image)
    : image_(image),
      configuration_(engine::require_native_data_image(image_).native_cell<ConfigLink>(0x434798BC),
                     image_->native_cell<HotkeyLink>(0x434798C4)) {
    // Early CRT callbacks can be registered before settings-owner construction.
    // This stage validates only the real BSS cells, without reading heap data.
    (void)image_->native_cell<std::uint32_t>(0x43478C68);
    (void)image_->native_cell<NativeConfigPath>(0x434798CC);
    (void)image_->native_cell<NativeConfigVector>(0x43478CB8);
}
std::uint32_t& PreservedConfigStorage::settings_cell() const {
    return image_->native_cell<std::uint32_t>(0x43478C68);
}
SettingsPersistenceState& PreservedConfigStorage::initialize_settings(engine::JobMemory memory) {
    (void)create_settings_owner(*image_, configuration_, memory);
    return settings();
}
SettingsPersistenceState& PreservedConfigStorage::settings() {
    if (!settings_) {
        auto state =
            std::make_unique<SettingsPersistenceState>(constructed_settings(*image_), 0x325);
        // Lengths follow433EEC8B's payloads; hash/type bytes remain live reads.
        constexpr std::array<std::uint16_t, 10> sizes{4, 8, 4, 4, 4, 4, 512, 4, 84, 1};
        for (std::size_t i = 0; i < sizes.size(); ++i)
            if (state->record_keys[i].size != sizes[i])
                throw std::logic_error("Native settings descriptor metadata is not constructed");
        settings_ = std::move(state);
    }
    if (reinterpret_cast<std::uintptr_t>(settings_->native_owner()) != settings_cell())
        throw std::logic_error("Constructed native settings owner was replaced or finalized");
    return *settings_;
}
NativeConfigPath& PreservedConfigStorage::path() const {
    return image_->native_cell<NativeConfigPath>(0x434798CC);
}
NativeConfigVector& PreservedConfigStorage::baseline_cells() const {
    return image_->native_cell<NativeConfigVector>(0x43478CB8);
}
std::span<const std::byte> PreservedConfigStorage::baseline() const {
    if (baseline_finalized_)
        throw std::logic_error("Native CONFIG baseline was terminally released");
    const auto& cells = baseline_cells();
    if (!cells.begin) {
        if (cells.end)
            throw std::logic_error("Native CONFIG baseline has no begin allocation");
        return {};
    }
    if (cells.end < cells.begin || cells.capacity < cells.end)
        throw std::logic_error("Native CONFIG baseline vector is not constructed");
    return {reinterpret_cast<const std::byte*>(std::uintptr_t(cells.begin)),
            std::size_t(cells.end - cells.begin)};
}
void PreservedConfigStorage::bind_source_menu_registry(ui::RootMenu& menu,
                                                       const ui::MainMenuRegistryName& name) {
    if (source_menu_ && (source_menu_ != &menu || source_registry_name_ != &name))
        throw std::logic_error("CONFIG registry name already belongs to another main menu");
    source_menu_ = &menu;
    source_registry_name_ = &name;
}
SettingsRegistryWindow PreservedConfigStorage::capture_registry_window() const {
    auto* const menu = image_->native_cell<ui::RootMenu*>(0x43476924u);
    if (!source_menu_ || !source_registry_name_ || menu != source_menu_)
        throw std::logic_error(
            "CONFIG requires the published source main menu and its registry name owner");
    return {source_registry_name_};
}
std::u16string_view
PreservedConfigStorage::registry_value_name(SettingsRegistryWindow window) const {
    if (!window.name)
        throw std::invalid_argument("Source settings registry name was not captured");
    return window.name->view();
}
NativeSettingsRegistryWindow PreservedConfigStorage::capture_native_registry_window() const {
    const auto address = image_->native_cell<std::uint32_t>(0x43476924);
    if (!address)
        throw std::logic_error("Settings persistence requires its constructed native main window");
    return {address};
}
std::u16string_view
PreservedConfigStorage::native_registry_value_name(NativeSettingsRegistryWindow window) const {
    if (!window.address)
        throw std::invalid_argument("Settings registry window was not captured");
    // RtlWriteRegistryValue receives this actual NUL-terminated inline UTF16
    // name. Reuse the saved window address even if the BSS slot was replaced
    // while433EEC8B ran; no source MenuWindow layout/address is assumed here.
    return {reinterpret_cast<const char16_t*>(std::uintptr_t(window.address) + 0xE0)};
}
void PreservedConfigStorage::finalize_settings(const engine::JobMemory& memory) {
    auto& cell = settings_cell();
    if (!cell)
        return;
    engine::free_native_allocation(reinterpret_cast<void*>(std::uintptr_t(cell)),
                                   terminal_memory(memory));
    cell = 0;
}
void PreservedConfigStorage::finalize_baseline(const engine::JobMemory& memory) {
    if (baseline_finalized_)
        throw std::logic_error("Native CONFIG baseline finalizer was invoked twice");
    const auto begin = baseline_cells().begin;
    if (begin)
        engine::free_native_allocation(reinterpret_cast<void*>(std::uintptr_t(begin)),
                                       terminal_memory(memory));
    baseline_finalized_ = true;
}
} // namespace skeet
