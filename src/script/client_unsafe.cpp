#include "client_detail.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/executable_pattern.hpp"
#include <array>
#include <cstring>
#include <limits>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::script {
namespace {
using namespace client_detail;
struct InterfaceRecord {
    void* (*create)();
    const char* name;
    InterfaceRecord* next;
};
std::byte* factory_export(std::byte* module) { //4334AC0B(module,CreateInterface hash,0,true)
    if (field<std::uint16_t>(module, 0) != 0x5a4d)
        return nullptr;
    auto* nt = module + field<std::uint32_t>(module, 0x3c);
    if (field<std::uint32_t>(nt, 0) != 0x4550 || field<std::uint16_t>(nt, 0x18) != 0x10b)
        return nullptr;
    const auto export_rva = field<std::uint32_t>(nt, 0x78);
    const auto export_size = field<std::uint32_t>(nt, 0x7c);
    if (!export_rva || !export_size)
        return nullptr;
    auto* exports = module + export_rva;
    const auto count = field<std::uint32_t>(exports, 0x18);
    const auto functions = field<std::uint32_t>(exports, 0x1c);
    if (!functions)
        return nullptr;
    const auto names = field<std::uint32_t>(exports, 0x20);
    const auto ordinals = field<std::uint32_t>(exports, 0x24);
    if (!names || !ordinals)
        return nullptr;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* name =
            reinterpret_cast<const char*>(module + field<std::uint32_t>(module + names, index * 4));
        if ((raw_name_hash(name) ^ 0x92ca5df1u) != 0x2fd78929u)
            continue;
        const auto ordinal = field<std::uint16_t>(module + ordinals, index * 2);
        const auto rva = field<std::uint32_t>(module + functions, ordinal * 4);
        if (rva >= export_rva && rva < export_rva + export_size)
            return nullptr;
        return module + rva;
    }
    return nullptr;
}
InterfaceRecord* interface_records(std::byte* module) { //43328E9C
    auto* entry = factory_export(module);
    if (!entry || entry[0] != std::byte{0x55} || entry[1] != std::byte{0x8b} ||
        entry[2] != std::byte{0xec})
        return nullptr;
    std::uintptr_t address = 0;
    if (entry[3] == std::byte{0x5d})
        address = field<std::uint32_t>(entry + field<std::int32_t>(entry, 5) + 0xf, 0);
    else if (entry[3] == std::byte{0x56})
        address = field<std::uint32_t>(entry, 6);
    else
        return nullptr;
    return *reinterpret_cast<InterfaceRecord**>(address);
}
struct SteamApiExportFallback {
    std::array<std::uint8_t, 8> prefix;
    std::size_t prefix_size;
    std::uint32_t expected_length;
    const char* export_name;
};
constexpr std::array<SteamApiExportFallback, 5> steam_api_fallbacks{{
    {{0x55, 0x8B, 0xEC, 0x83, 0x3D}, 5, 28, "SteamAPI_RegisterCallResult"},
    {{0x55, 0x8B, 0xEC, 0xFF, 0x75, 0x10}, 6, 9, "SteamAPI_UnregisterCallResult"},
    {{0x55, 0x8B, 0xEC, 0x83, 0x3D}, 5, 27, "SteamAPI_RegisterCallback"},
    {{0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x80, 0x3D}, 8, 15, "SteamAPI_UnregisterCallback"},
    {{0x32, 0xC9, 0x83, 0x3D}, 4, 9, "SteamAPI_RunCallbacks"},
}};
void* resolve_steam_api_fallback(std::byte* module, const std::uint8_t* pattern,
                                 std::uint32_t length) {
    if (!module || !pattern || !length)
        return nullptr;
    for (const auto& entry : steam_api_fallbacks) {
        if (length >= entry.prefix_size && length <= entry.expected_length + 4 &&
            std::memcmp(pattern, entry.prefix.data(), entry.prefix_size) == 0) {
            if (entry.prefix_size == 5 && entry.export_name[11] == 'R') {
                // Distinguish RegisterCallResult (ends with ff 75 10) vs RegisterCallback (c7 05)
                if (length >= 2 && pattern[length - 2] == 0xC7 && pattern[length - 1] == 0x05)
                    return reinterpret_cast<void*>(
                        GetProcAddress(reinterpret_cast<HMODULE>(module), "SteamAPI_RegisterCallback"));
                return reinterpret_cast<void*>(
                    GetProcAddress(reinterpret_cast<HMODULE>(module), "SteamAPI_RegisterCallResult"));
            }
            return reinterpret_cast<void*>(
                GetProcAddress(reinterpret_cast<HMODULE>(module), entry.export_name));
        }
    }
    return nullptr;
}
bool valid_pe32_module(const std::byte* module) noexcept {
    if (!module || field<std::uint16_t>(module, 0) != 0x5A4Du)
        return false;
    const auto nt_offset = field<std::uint32_t>(module, 0x3Cu);
    if (nt_offset < 0x40u || nt_offset > 0x1000u)
        return false;
    const auto* nt = module + nt_offset;
    return field<std::uint32_t>(nt, 0) == 0x4550u && field<std::uint16_t>(nt, 0x18) == 0x10Bu;
}
}
int ClientModule::unsafe(Export operation, void* state) {
    const auto& api = *bridge().api;
    if (!(bindings.unsafe_access && *bindings.unsafe_access) &&
        !(bindings.unsafe_builtin && *bindings.unsafe_builtin) &&
        !(bindings.initializing && *bindings.initializing))
        return api.lerror(state, "Blocked unsafe script");
    if (api.gettop(state) < 2)
        return 0;
    std::size_t length = 0;
    const auto* module_name = api.tolstring(state, 1, &length);
    if (!module_name || !length)
        return 0;
    const auto* argument = api.tolstring(state, 2, &length);
    if (!argument || !length)
        return 0;
    auto hash = raw_name_hash(module_name, true) ^ 0x92ca5df1u;
    const char* lookup_name = module_name;
    if (hash == 0x07b9bda7u) {
        hash = 0xfe901e85u; // native alias maps to client.dll
        lookup_name = "client.dll";
    } else if (hash == 0xbbffa7e0u)
        return 0;
    auto* module = static_cast<std::byte*>(
        bindings.loaded_modules
            ? engine::find_loaded_module(*bindings.loaded_modules, hash, true, false)
            : engine::find_loaded_module(hash, true, false));
    if (!module)
        module = reinterpret_cast<std::byte*>(GetModuleHandleA(lookup_name));
    if (!valid_pe32_module(module))
        return 0;
    if (operation == Export::find_signature) {
        auto* found =
            engine::executable_pattern(module, reinterpret_cast<const std::uint8_t*>(argument),
                                       static_cast<std::uint32_t>(length));
        if (!found && hash == 0xab195f77u) {
            found = static_cast<std::byte*>(resolve_steam_api_fallback(
                module, reinterpret_cast<const std::uint8_t*>(argument),
                static_cast<std::uint32_t>(length)));
        }
        if (!found)
            return 0;
        api.pushlightuserdata(state, found);
        return 1;
    }
    const auto wanted = raw_name_hash(argument) ^ 0x92ca5df1u;
    for (auto* record = interface_records(module); record; record = record->next)
        if ((raw_name_hash(record->name) ^ 0x92ca5df1u) == wanted) {
            api.pushlightuserdata(state, record->create());
            return 1;
        }
    if (auto* factory =
            reinterpret_cast<void*(__cdecl*)(const char*, int*)>(factory_export(module))) {
        int rc = 1;
        if (void* instance = factory(argument, &rc)) {
            api.pushlightuserdata(state, instance);
            return 1;
        }
    }
    return 0;
}
} // namespace skeet::script
