#include "skeet/config_storage/owner.hpp"
#include "skeet/config_storage/preset_runtime.hpp"
#include "skeet/config_storage/preserved.hpp"
#include "skeet/config_storage/settings_capture.hpp"
#include "skeet/menu/misc_menu.hpp"
#include "skeet/menu/scripts_menu.hpp"
#include "../script/files_detail.hpp"
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

extern "C" __declspec(dllimport) NTSTATUS NTAPI NtReadFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                                                           PIO_STATUS_BLOCK, PVOID, ULONG,
                                                           PLARGE_INTEGER, PULONG);
extern "C" __declspec(dllimport) NTSTATUS NTAPI NtDeleteFile(POBJECT_ATTRIBUTES);
extern "C" __declspec(dllimport) NTSTATUS NTAPI RtlWriteRegistryValue(ULONG, PCWSTR, PCWSTR, ULONG,
                                                                      PVOID, ULONG);
using LoadedModuleCallback = void(NTAPI*)(void*, void*, BOOLEAN*);
extern "C" __declspec(dllimport) NTSTATUS NTAPI LdrEnumerateLoadedModules(ULONG,
                                                                          LoadedModuleCallback,
                                                                          void*);

namespace skeet {
namespace {
UNICODE_STRING borrowed_name(const NativeConfigPath& name) noexcept {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    // The canonical path was created as a UNICODE_STRING by the initializer;
    // its counted length is retained, including an empty allocated name.
    UNICODE_STRING out;
    out.Length = name.length;
    out.MaximumLength = name.maximum_length;
    out.Buffer = reinterpret_cast<wchar_t*>(name.buffer);
    return out;
}
struct LoaderEntry {
    LIST_ENTRY load, memory, initialization;
    void* base;
    void* entry;
    ULONG image_size;
    UNICODE_STRING full_name, base_name;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(LoaderEntry, base_name) == 0x2C);
static_assert(offsetof(NativeConfigPath, buffer) == 4);
#endif
struct ModuleVisit {
    SettingsPersistenceBackend::ModuleVisitor visitor;
    void* context;
};
void NTAPI visit_module(void* raw, void* parameter, BOOLEAN* stop) {
    if (!raw || !parameter || !stop)
        return;
    const auto& entry = *static_cast<const LoaderEntry*>(raw);
    if (!entry.base_name.Buffer || !entry.base_name.Length)
        return;
    const auto& visit = *static_cast<const ModuleVisit*>(parameter);
    if (!visit.visitor(visit.context, {reinterpret_cast<const char16_t*>(entry.base_name.Buffer),
                                       std::size_t(entry.base_name.Length) / sizeof(char16_t)}))
        *stop = TRUE;
}
}

std::optional<ConfigBytes> read_native_file(const NativeConfigPath& native_path) {
    auto path = borrowed_name(native_path);
    if (!path.Buffer)
        return std::nullopt;
    script::files_detail::File file;
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &path, 0x40, nullptr, nullptr);
    IO_STATUS_BLOCK status{};
    if (NtOpenFile(&file.handle, 0x120089, &attributes, &status, 7, 0x60) < 0) {
        file.handle = nullptr;
        return std::nullopt;
    }
    struct StandardInformation {
        LARGE_INTEGER allocation, end;
        ULONG links;
        BOOLEAN deleted, directory;
    } information;
    if (NtQueryInformationFile(file.handle, &status, &information, sizeof information,
                               FileStandardInformation) < 0)
        return std::nullopt;
    const auto count = information.end.LowPart;
    // Native43387448 returns null when the wrapped allocation request is zero.
    const auto capacity = std::uint32_t(count + 1U);
    if (!capacity)
        return std::nullopt;
    std::unique_ptr<std::byte[]> memory(new (std::nothrow) std::byte[capacity]{});
    if (!memory)
        return std::nullopt;
    auto result = NtReadFile(file.handle, nullptr, nullptr, nullptr, &status, memory.get(), count,
                             nullptr, nullptr);
    if (result == 0x103) {
        result = NtWaitForSingleObject(file.handle, FALSE, nullptr);
        if (result >= 0)
            result = status.Status;
    }
    if (result < 0 && std::uint32_t(result) != 0xC0000011U)
        return std::nullopt;
    return ConfigBytes(memory.get(), memory.get() + count);
}
std::optional<ConfigBytes> NativeNamedConfigStorage::read_container() {
    return read_native_file(native_path_);
}

void NativeNamedConfigStorage::write_container(std::span<const std::byte> bytes) {
    // Reuse the actual433F9C97 open/create/truncate helper, without a second
    // UTF-8 path conversion or an owner-local alternative filename.
    script::files_detail::NativePath path;
    path.text = borrowed_name(native_path_);
    script::files_detail::File file{script::files_detail::open_write(path)};
    if (file.handle) {
        DWORD written;
        WriteFile(file.handle, bytes.data(), DWORD(bytes.size()), &written, nullptr);
    }
}

void NativeNamedConfigStorage::remove_container() {
    auto path = borrowed_name(native_path_);
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &path, 0x40, nullptr, nullptr);
    (void)NtDeleteFile(&attributes);
}

void NativeSettingsPersistenceBackend::enumerate_loaded_modules(ModuleVisitor visitor,
                                                                void* context) {
    ModuleVisit visit{visitor, context};
    (void)LdrEnumerateLoadedModules(0, visit_module, &visit);
}
void NativeSettingsPersistenceBackend::write_registry_value(std::uint32_t relative_to,
                                                            std::u16string_view path,
                                                            std::u16string_view value_name,
                                                            std::uint32_t type,
                                                            std::span<const std::byte> data) {
    // SettingsPersistence supplies the native saved-window name or its owned
    // standalone string; both names and the literal Software path are terminated.
    (void)RtlWriteRegistryValue(relative_to, reinterpret_cast<const wchar_t*>(path.data()),
                                reinterpret_cast<const wchar_t*>(value_name.data()), type,
                                const_cast<std::byte*>(data.data()), ULONG(data.size()));
}

struct ConfigStorageOwner::Impl {
    std::shared_ptr<PreservedConfigStorage> native;
    ConfigRegistries& configuration;
    ui::RageClipboardApi& clipboard;
    NativeNamedConfigStorage files;
    NativeSettingsPersistenceBackend system;
    MenuSettingsCapture capture;
    SettingsPersistence persistence;
    PresetRuntime runtime;
    NamedConfigStore store;
    ConfigPresetsManager presets;
    ui::RootMenu* menu = nullptr;
    Impl(ConfigRegistries& registry, std::shared_ptr<PreservedConfigStorage> storage,
         ui::MenuWindow& window, script::Bridge& bridge, script::UiBindings& script_ui,
         ui::SettingsMenuValues settings_values, ui::ScriptsMenuValues script_values)
        : native(std::move(storage)), configuration(require_registry(registry)),
          clipboard(ui::system_rage_clipboard()), files(native->path()),
          capture(window, settings_values, script_values),
          persistence(system, *native, [this] { return capture(); }),
          runtime(window, bridge, script_ui, persistence, clipboard), store(files),
          presets(configuration, store, *native, runtime) {}
    PreservedConfigStorage& require_native() {
        if (!native)
            throw std::invalid_argument("CONFIG requires its canonical preserved native storage");
        return *native;
    }
    ConfigRegistries& require_registry(ConfigRegistries& registry) {
        if (&require_native().configuration() != &registry)
            throw std::invalid_argument(
                "CONFIG storage requires the same retained-image registry view as native startup");
        return registry;
    }
};

ConfigStorageOwner::ConfigStorageOwner(ConfigRegistries& registry,
                                       std::shared_ptr<PreservedConfigStorage> native,
                                       ui::MenuWindow& window, script::Bridge& bridge,
                                       script::UiBindings& script_ui,
                                       ui::SettingsMenuValues settings_values,
                                       ui::ScriptsMenuValues script_values)
    : impl_(std::make_unique<Impl>(registry, std::move(native), window, bridge, script_ui,
                                   settings_values, script_values)) {}
ConfigStorageOwner::~ConfigStorageOwner() = default;
ConfigRegistries& ConfigStorageOwner::configuration() noexcept {
    return impl_->configuration;
}
SettingsPersistenceState& ConfigStorageOwner::settings_state() {
    return impl_->native->settings();
}
PreservedConfigStorage& ConfigStorageOwner::native_storage() noexcept {
    return *impl_->native;
}
NamedConfigStore& ConfigStorageOwner::named() noexcept {
    return impl_->store;
}
ConfigPresetsManager& ConfigStorageOwner::presets() noexcept {
    return impl_->presets;
}
PresetRuntime& ConfigStorageOwner::runtime() noexcept {
    return impl_->runtime;
}
MenuSettingsCapture& ConfigStorageOwner::capture() noexcept {
    return impl_->capture;
}
SettingsPersistence& ConfigStorageOwner::persistence() noexcept {
    return impl_->persistence;
}
ui::RageClipboardApi& ConfigStorageOwner::clipboard() noexcept {
    return impl_->clipboard;
}
void ConfigStorageOwner::bind_menu(ui::RootMenu& menu, script::ScriptManager& scripts) {
    if (impl_->menu)
        throw std::logic_error("Configuration storage is already bound to its menu");
    impl_->runtime.bind_menu(menu, impl_->presets, scripts);
    impl_->capture.bind_menu(menu, scripts);
    impl_->menu = &menu;
}
bool ConfigStorageOwner::menu_bound_to(const ui::RootMenu& menu) const noexcept {
    return impl_->menu == &menu;
}
} // namespace skeet
