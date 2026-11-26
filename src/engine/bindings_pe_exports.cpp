#include "skeet/engine/pe_exports.hpp"
#include "skeet/engine/netvars.hpp"
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
template <class Value> Value read(const std::byte* address) {
    Value value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
#if defined(_WIN32)
const std::byte* WINAPI native_nt_header(const std::byte* module) {
    using Function = PIMAGE_NT_HEADERS(WINAPI*)(PVOID);
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto address = GetProcAddress(native, "RtlImageNtHeader");
    if (!address)
        throw std::runtime_error("ntdll!RtlImageNtHeader export is missing");
    const auto function = reinterpret_cast<Function>(address);
    return reinterpret_cast<const std::byte*>(function(const_cast<std::byte*>(module)));
}
#endif
}

NativeImageEnvironment native_image_environment() {
#if defined(_WIN32)
    return {&native_nt_header};
#else
    throw std::runtime_error("Native image exports require Windows");
#endif
}

std::byte* resolve_pe_export(std::byte* module, std::uint32_t hash, std::uint32_t ordinal,
                             bool salted_hash, NativeImageEnvironment environment) {
    if (!module)
        return nullptr;
    const auto* headers = environment.image_nt_header(module);
    if (!headers || read<std::uint16_t>(headers + 0x18) != 0x10b)
        return nullptr;
    const auto directory_rva = read<std::uint32_t>(headers + 0x78);
    const auto directory_size = read<std::uint32_t>(headers + 0x7c);
    if (!directory_rva || !directory_size)
        return nullptr;
    const auto* directory = module + directory_rva;
    const auto functions_rva = read<std::uint32_t>(directory + 0x1c);
    const auto ordinals_rva = read<std::uint32_t>(directory + 0x24);
    const auto names_rva = read<std::uint32_t>(directory + 0x20);
    const auto* functions = functions_rva ? module + functions_rva : nullptr;
    const auto* ordinals = ordinals_rva ? module + ordinals_rva : nullptr;
    const auto* names = names_rva ? module + names_rva : nullptr;
    const auto wanted = hash && salted_hash ? hash ^ 0x92ca5df1 : hash;
    const auto count = read<std::uint32_t>(directory + (hash ? 0x18 : 0x14));
    for (std::uint32_t index = 0; index != count; ++index) {
        std::uint32_t function_rva;
        if (!wanted) {
            if (ordinal != read<std::uint32_t>(directory + 0x10) + index)
                continue;
            function_rva = read<std::uint32_t>(functions + index * 4);
        } else {
            const auto name_rva = read<std::uint32_t>(names + index * 4);
            const auto actual = netvar_name_hash(reinterpret_cast<const char*>(module + name_rva));
            if (wanted != (actual ^ 0x92ca5df1))
                continue;
            const auto number = read<std::uint16_t>(ordinals + index * 2);
            function_rva = read<std::uint32_t>(functions + number * 4);
        }
        const auto directory_end = directory_rva + directory_size;
        if (function_rva >= directory_rva && function_rva < directory_end)
            return nullptr;
        return module + function_rva;
    }
    return nullptr;
}

std::byte* resolve_pe_export(std::byte* module, std::uint32_t hash, std::uint32_t ordinal,
                             bool salted_hash) {
    return resolve_pe_export(module, hash, ordinal, salted_hash, native_image_environment());
}

InterfaceRegistration* find_interface_registry(std::byte* module,
                                               NativeImageEnvironment environment) {
    auto* factory = resolve_pe_export(module, 0x2fd78929, 0, true, environment);
    if (!factory || factory[0] != std::byte{0x55} || factory[1] != std::byte{0x8b} ||
        factory[2] != std::byte{0xec})
        return nullptr;
    std::uint32_t pointer_address;
    if (factory[3] == std::byte{0x5d}) {
        const auto relative = read<std::int32_t>(factory + 5);
        pointer_address = read<std::uint32_t>(factory + relative + 15);
    } else if (factory[3] == std::byte{0x56}) {
        pointer_address = read<std::uint32_t>(factory + 6);
    } else {
        return nullptr;
    }
    return *reinterpret_cast<InterfaceRegistration**>(static_cast<std::uintptr_t>(pointer_address));
}
InterfaceRegistration* find_interface_registry(std::byte* module) {
    return find_interface_registry(module, native_image_environment());
}

InterfaceRegistration* find_interface_registration(InterfaceRegistration* first,
                                                   std::uint32_t hash) noexcept {
    for (auto* node = first; node; node = node->next)
        if (netvar_name_hash(node->name) == hash)
            return node;
    return nullptr;
}
} // namespace skeet::engine
