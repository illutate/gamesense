#include "skeet/engine/forwarded_exports.hpp"
#include "skeet/engine/netvars.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* address) {
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
std::uint32_t module_hash(const char* name) noexcept { // 0x4334DEFF
    std::uint32_t hash = 0x811c9dc5;
    for (auto value = static_cast<std::uint8_t>(*name); value;
         value = static_cast<std::uint8_t>(*++name)) {
        if (static_cast<std::uint8_t>(value - 'A') <= 'Z' - 'A')
            value += 'a' - 'A';
        hash = (hash ^ value) * 0x1000193;
    }
    return hash ^ 0x92ca5df1;
}
std::uint32_t export_string_hash(const ExportAnsiString& name) noexcept { // 0x4333D6A2
    std::uint32_t hash = 0x811c9dc5;
    for (std::uint32_t i = 0; i != name.length; ++i)
        hash = (hash ^ static_cast<std::uint8_t>(name.buffer[i])) * 0x1000193;
    return hash ^ 0x92ca5df1;
}

std::byte* resolve_directory_export(std::byte* module, std::uint32_t hash,
                                    std::uint32_t ordinal_token, std::byte* directory,
                                    std::uint32_t directory_size, bool salted_hash,
                                    bool lock_loader,
                                    const ForwardedExportEnvironment& environment) {
    // Native 0x433EC666 keeps the selected export index in a 16-bit value.
    std::uint16_t index;
    if (ordinal_token & 0x80000000u) {
        index = static_cast<std::uint16_t>(ordinal_token - read<std::uint16_t>(directory + 0x10));
    } else {
        const auto wanted = salted_hash ? hash ^ 0x92ca5df1u : hash;
        const auto names = module + read<std::uint32_t>(directory + 0x20);
        const auto ordinals = module + read<std::uint32_t>(directory + 0x24);
        const auto count = read<std::uint32_t>(directory + 0x18);
        index = 0xffff;
        for (std::uint32_t i = 0; i != count; ++i) {
            const auto name =
                reinterpret_cast<const char*>(module + read<std::uint32_t>(names + i * 4));
            if ((netvar_name_hash(name) ^ 0x92ca5df1u) == wanted) {
                index = read<std::uint16_t>(ordinals + i * 2);
                break;
            }
        }
    }
    if (index >= read<std::uint32_t>(directory + 0x14))
        return nullptr;
    const auto functions = module + read<std::uint32_t>(directory + 0x1c);
    const auto function_rva = read<std::uint32_t>(functions + index * 4);
    auto* const function = module + function_rva;
    // The native lower bound is strict: the directory's first byte is not
    // classified as a forwarder, unlike an address strictly inside it.
    if (function <= directory || function >= directory + directory_size)
        return function_rva ? function : nullptr;

    const auto* forwarder = reinterpret_cast<const char*>(function);
    const auto* separator = std::strchr(forwarder, '.');
    const auto module_length = static_cast<std::uint16_t>(separator - forwarder);
    std::string target_name(forwarder, module_length);
    target_name += ".dll";
    auto* target = static_cast<std::byte*>(find_loaded_module(
        environment.modules, module_hash(target_name.c_str()), true, lock_loader));
    if (!target)
        return nullptr;

    ExportAnsiString symbol{};
    environment.initialize_string(&symbol, forwarder + module_length + 1);
    std::uint32_t target_ordinal = 0;
    std::uint32_t target_hash = 0;
    if (symbol.length > 1 && symbol.buffer[0] == '#') {
        if (environment.ordinal_from_string(symbol.buffer + 1, 0, &target_ordinal) < 0)
            return nullptr;
    } else {
        target_hash = export_string_hash(symbol);
    }
    return resolve_forwarded_export(target, target_hash, target_ordinal, true, lock_loader,
                                    environment);
}
}

std::byte* resolve_forwarded_export(std::byte* module, std::uint32_t hash, std::uint32_t ordinal,
                                    bool salted_hash, bool lock_loader,
                                    const ForwardedExportEnvironment& environment) { // 0x43320B0A
    if (!hash && !ordinal)
        return nullptr;
    const auto ordinal_token = hash ? 0u : ordinal | 0x80000000u;
    std::uint32_t directory_size = ordinal;
    auto* directory = environment.image_directory_entry(module, 1, 0, &directory_size);
    if (!directory)
        return nullptr;
    return resolve_directory_export(module, hash, ordinal_token, directory, directory_size,
                                    salted_hash, lock_loader, environment);
}

ForwardedExportEnvironment native_forwarded_export_environment() {
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto directory = GetProcAddress(native, "RtlImageDirectoryEntryToData");
    const auto string = GetProcAddress(native, "RtlInitAnsiString");
    const auto ordinal = GetProcAddress(native, "RtlCharToInteger");
    if (!directory || !string || !ordinal)
        throw std::runtime_error("Native forwarded-export dependency is missing");
    return {native_loaded_module_environment(), reinterpret_cast<ImageDirectoryEntry>(directory),
            reinterpret_cast<InitializeExportString>(string),
            reinterpret_cast<ExportOrdinalFromString>(ordinal)};
#else
    throw std::runtime_error("Native forwarded-export resolution requires Windows");
#endif
}
}
