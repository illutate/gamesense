#include "skeet/input/native_window_lifecycle.hpp"
#include "skeet/client/command_phase_registry.hpp"
#include "skeet/config_storage/settings_persistence.hpp"
#include "skeet/network/update_observers.hpp"
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#endif

namespace skeet::input {
namespace {
constexpr char16_t mutex_name[] = u"$ IDA registry mutex $";
template <class Character>
std::uint32_t signature_hash(const Character* text) noexcept { //4334ADB0/43359C70
    std::uint32_t value = 0x811C9DC5;
    if (text)
        for (; *text; ++text)
            value = (value ^ static_cast<std::make_unsigned_t<Character>>(*text)) * 0x1000193u;
    return value ^ 0x92CA5DF1;
}
bool time_expired(std::int64_t now, std::int64_t then) noexcept {
    const auto difference = std::uint64_t(now) - std::uint64_t(then);
    const auto sign = std::uint64_t(0) - (difference >> 63);
    const auto magnitude = (difference ^ sign) - sign;
    // The native signed absolute value leaves INT64_MIN negative.
    return magnitude < (std::uint64_t(1) << 63) && magnitude > 10;
}
#if defined(_WIN32)
template <class Function> Function required_export(const char* library, const char* name) {
    const auto module = GetModuleHandleA(library);
    const auto function = module ? GetProcAddress(module, name) : nullptr;
    if (!function)
        throw std::runtime_error("Required native window-lifecycle export is missing");
    return reinterpret_cast<Function>(function);
}
std::uint32_t current_process_id() {
#if defined(_M_IX86) || defined(__i386__)
    return __readfsdword(0x20);
#else
    return static_cast<std::uint32_t>(__readgsqword(0x40));
#endif
}
#endif
}

NativeWindowLifecycleImports native_window_lifecycle_imports() {
#if defined(_WIN32)
    using Imports = NativeWindowLifecycleImports;
    return {required_export<Imports::Foreground>("user32.dll", "GetForegroundWindow"),
            required_export<Imports::OpenMutexFunction>("kernel32.dll", "OpenMutexW"),
            required_export<Imports::Close>("ntdll.dll", "ZwClose"),
            required_export<Imports::Delete>("ntdll.dll", "ZwDeleteFile"),
            required_export<Imports::Time>("msvcrt.dll", "_time64"),
            required_export<Imports::DefaultProcedure>("ntdll.dll", "NtdllDefWindowProc_W"),
            required_export<Imports::ModuleFileName>("kernel32.dll", "GetModuleFileNameA"),
            required_export<Imports::TempPath>("kernel32.dll", "GetTempPathW"),
            required_export<Imports::DosToNtPath>("ntdll.dll", "RtlDosPathNameToNtPathName_U"),
            required_export<Imports::FreeUnicode>("ntdll.dll", "RtlFreeUnicodeString"),
            required_export<Imports::Format>("msvcrt.dll", "sprintf_s"),
            required_export<Imports::AppendWide>("msvcrt.dll", "wcscat_s"),
            current_process_id};
#else
    throw std::runtime_error("Native window lifecycle requires Windows");
#endif
}

NativeWindowLifecycle::NativeWindowLifecycle(SettingsPersistence& persistence,
                                             NativeWindowLifecycleImports imports,
                                             engine::LoadedModuleEnvironment modules,
                                             engine::NativeImageEnvironment images)
    : persistence_(persistence), imports_(imports), modules_(modules), images_(images) {}

NativeWindowLifecycle::~NativeWindowLifecycle() { //433EBEDB
    if (dump_path.buffer)
        imports_.free_unicode(&dump_path);
}

std::int32_t NativeWindowLifecycle::delete_dump() {
    const NativeWindowObjectAttributes attributes{
        sizeof(NativeWindowObjectAttributes), 0, &dump_path, 0x40, nullptr, nullptr};
    return imports_.delete_file(&attributes);
}

void NativeWindowLifecycle::initialize() { //4348FD50
    auto* module =
        static_cast<std::byte*>(engine::find_loaded_module(modules_, 0xA42BE2F8, true, true));
    if (module) {
        class_name = reinterpret_cast<NativeWindowLifecycleImports::ClassName>(
            engine::resolve_pe_export(module, 0xC4A57DC2, 0, true, images_));
        foreground = reinterpret_cast<NativeWindowLifecycleImports::Foreground>(
            engine::resolve_pe_export(module, 0xDAA66BB8, 0, true, images_));
        internal_text = reinterpret_cast<NativeWindowLifecycleImports::InternalText>(
            engine::resolve_pe_export(module, 0x0306EBC1, 0, true, images_));
        window_list = reinterpret_cast<NativeWindowLifecycleImports::WindowList>(
            engine::resolve_pe_export(module, 0x087A9C92, 0, true, images_));
    }
    // Native initialization takes the second InLoadOrder module (ntdll).
    auto* native = reinterpret_cast<engine::LoaderModule*>(modules_.loader->modules.next->next);
    crc32 = reinterpret_cast<NativeWindowLifecycleImports::Crc32>(engine::resolve_pe_export(
        static_cast<std::byte*>(native->base), 0xA0681B30, 0, true, images_));

    std::array<std::uint32_t, 2> properties{0xFF123456, 0xFF123456};
    std::array<char, 260> executable{};
    if (imports_.module_file_name(0, executable.data(), executable.size())) {
        auto* separator = std::strrchr(executable.data(), '\\');
        if (!separator)
            separator = std::strrchr(executable.data(), '/');
        if (separator) {
            std::array<char, 128> title{};
            const auto process = imports_.current_process_id();
            imports_.format(title.data(), title.size(), "%s (%u) Properties", separator + 1,
                            process);
            properties[0] = signature_hash(title.data());
            imports_.format(title.data(), title.size(), "Eigenschaften von %s (%u)", separator + 1,
                            process);
            properties[1] = signature_hash(title.data());
        }
    }
    title_signatures = {0xB7774305, 0x402DF8B7, 0x7670079D,    0x8CA2613C,
                        0xFB81064D, 0x07EB0226, 0xC095ECA1,    0x613BACDE,
                        0xF6115A6E, 0x7ED6BAB9, properties[0], properties[1]};
    class_signatures = {0xDADC818D, 0x85A6C949, 0x2CF0DE78, 0xC4927E1E,
                        0x0A432040, 0x56AC0ADE, 0x28477FBE, 0x5178FBF8};
    paired_signatures = {{{0x8A6E61AA, 0xFEE2FC64}, {0xCA172992, 0x4F61CAF5}}};

    std::array<char16_t, 260> path{};
    imports_.temp_path(path.size(), path.data());
    imports_.append_wide(path.data(), path.size(), u"csgo.DMP");
    imports_.dos_to_nt_path(path.data(), &dump_path, nullptr, nullptr);
    path.fill(0);
    (void)delete_dump();
}

std::uint32_t NativeWindowLifecycle::scan() {
    if (const auto mutex = imports_.open_mutex(0x20000, 0, mutex_name)) {
        imports_.close(mutex);
        return 101;
    }
    if (deletion_pending && dump_path.buffer) {
        if (delete_dump() >= 0) {
            deletion_pending = false;
            return 200;
        }
        if (time_expired(imports_.time(nullptr), detection_time))
            deletion_pending = false;
    }
    if (!window_list)
        return 0;
    std::array<std::uintptr_t, 1024> windows;
    std::uint32_t count = 0;
    if (window_list(0, 0, 0, 0, 0, windows.size(), windows.data(), &count) < 0)
        return 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t class_hash = 0, title_hash = 0;
        std::array<char16_t, 1024> text{};
        if (class_name) {
            NativeWindowUnicodeString value{0, 0x800, text.data()};
            const auto length = class_name(windows[index], 0, &value);
            if (length >= 6) {
                class_hash = signature_hash(text.data());
                for (std::uint32_t match = 0; match < class_signatures.size(); ++match)
                    if (class_signatures[match] == class_hash)
                        return match + 1;
                if (length >= 19) {
                    text[18] = 0;
                    if (signature_hash(text.data()) == 0x690AB6B7)
                        return 8;
                }
            }
        }
        text.fill(0);
        if (imports_.default_window_procedure(windows[index], 0xD, text.size(),
                                              reinterpret_cast<std::intptr_t>(text.data())) >= 4) {
            title_hash = signature_hash(text.data());
            for (std::uint32_t match = 0; match < title_signatures.size(); ++match)
                if (title_signatures[match] == title_hash)
                    return match + 101;
            if (class_hash == pending_signature[0] && title_hash == pending_signature[1]) {
                deletion_pending = true;
                detection_time = imports_.time(nullptr);
            }
        }
        for (std::uint32_t match = 0; match < paired_signatures.size(); ++match)
            if (paired_signatures[match][0] == class_hash &&
                paired_signatures[match][1] == title_hash)
                return match + 201;
    }
    return 0;
}

void NativeWindowLifecycle::poll() { //433E291F
    const auto window = (foreground ? foreground : imports_.get_foreground_window)();
    if (!window)
        return;
    last_foreground = window;
    if (const auto reason = scan())
        persistence_.persist(reason, 0);
}
void NativeWindowLifecycle::poll_foreground_changed() { //4339879F, receiver is native owner+10
    const auto window = (foreground ? foreground : imports_.get_foreground_window)();
    if (!window || window == last_foreground)
        return;
    last_foreground = window;
    if (const auto reason = scan())
        persistence_.persist(reason, 0);
}

namespace {
#if defined(_M_IX86) || defined(__i386__)
#define WINDOW_MEMBER __thiscall
#define WINDOW_FAST __fastcall
#else
#define WINDOW_MEMBER
#define WINDOW_FAST
#endif
using OwnerEntry = void(WINDOW_MEMBER*)(void*);
struct WindowLifecycleOwner;
struct UpdateEndpoint : network::UpdateObserver {
    WindowLifecycleOwner* owner;
};
void WINDOW_MEMBER destroy_owner(void*);
void WINDOW_MEMBER initialize_owner(void*);
void WINDOW_MEMBER poll_owner(void*);
void WINDOW_MEMBER poll_changed(void*);
void WINDOW_MEMBER unchanged(void*) {} //433CCE8E
void WINDOW_FAST unchanged_player(void*, void*, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
} //4337D653
void WINDOW_FAST unchanged_view(void*, float*, float*, float*) {} //4334A269
void WINDOW_FAST unchanged_post_view(void*, void*) {} //433CCE8E
const std::array<OwnerEntry, 9> owner_table{destroy_owner, initialize_owner, unchanged,
                                            unchanged,     unchanged,        unchanged,
                                            poll_owner,    unchanged,        unchanged};
const network::UpdateObserverVtable update_table{
    {poll_changed, unchanged, unchanged, unchanged, unchanged},
    unchanged_player,
    unchanged_view,
    unchanged_post_view};
struct WindowLifecycleOwner {
    const OwnerEntry* vtable = nullptr;
    std::uintptr_t reserved = 0;
    client::NativeCommandPhaseReceiver command;
    UpdateEndpoint update{{nullptr}, this};
    NativeWindowLifecycle runtime;
    engine::JobMemory memory;
    WindowLifecycleOwner(engine::OwnerRegistryBinding owners,
                         client::CommandPhaseRegistry& commands,
                         engine::OwnerRegistryBinding updates, SettingsPersistence& persistence,
                         NativeWindowLifecycleImports imports,
                         engine::LoadedModuleEnvironment modules,
                         engine::NativeImageEnvironment images)
        : runtime(persistence, imports, modules, images), memory(owners.memory) {
        owners.append(this); //433676A0
        commands.append(command, &runtime); //43345EBC, canonical434757A0 storage
        network::register_update_observer(updates, update); //433B8C1C
        vtable = owner_table.data();
        commands.publish(command, client::CommandOwnerId::owner_4346eed8); //4347F584
        update.vtable = &update_table;
    }
};
void WINDOW_MEMBER destroy_owner(void* object) { //43387ABC ->433EBEDB ->433837D0
    auto* owner = static_cast<WindowLifecycleOwner*>(object);
    const auto memory = owner->memory;
    owner->~WindowLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void WINDOW_MEMBER initialize_owner(void* object) {
    static_cast<WindowLifecycleOwner*>(object)->runtime.initialize();
}
void WINDOW_MEMBER poll_owner(void* object) {
    static_cast<WindowLifecycleOwner*>(object)->runtime.poll();
}
void WINDOW_MEMBER poll_changed(void* object) {
    static_cast<UpdateEndpoint*>(object)->owner->runtime.poll_foreground_changed();
}
#undef WINDOW_MEMBER
#undef WINDOW_FAST
}

std::size_t native_window_lifecycle_owner_size() noexcept {
    return sizeof(WindowLifecycleOwner);
}
NativeWindowLifecycle& construct_native_window_lifecycle(
    void* storage, engine::OwnerRegistryBinding owners, client::CommandPhaseRegistry& commands,
    engine::OwnerRegistryBinding updates, SettingsPersistence& persistence,
    NativeWindowLifecycleImports imports, engine::LoadedModuleEnvironment modules,
    engine::NativeImageEnvironment images) {
    auto* owner = ::new (storage)
        WindowLifecycleOwner(owners, commands, updates, persistence, imports, modules, images);
    return owner->runtime;
}
} // namespace skeet::input
