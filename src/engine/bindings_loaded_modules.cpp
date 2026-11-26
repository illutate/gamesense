#include "skeet/engine/loaded_modules.hpp"
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#endif

namespace skeet::engine {
std::uint32_t loaded_module_name_hash(const char16_t* name, std::int32_t length) noexcept {
    std::uint32_t hash = 0x811c9dc5;
    auto remaining = static_cast<std::uint32_t>(length);
    if (name) {
        while (*name && remaining != 0) {
            auto value = static_cast<std::uint16_t>(*name++);
            if (static_cast<std::uint16_t>(value - 0x41) <= 0x19)
                value = static_cast<std::uint16_t>(value + 0x20);
            hash = (hash ^ value) * 0x1000193;
            --remaining;
        }
    }
    return hash ^ 0x92ca5df1;
}

void* find_loaded_module(const LoadedModuleEnvironment& environment, std::uint32_t hash,
                         bool salted_hash, bool lock_loader) {
    if (!environment.loader)
        return nullptr;
    auto* const sentinel = &environment.loader->modules;
    // Native433F29C5 captures this first link before acquiring the PEB lock.
    auto* current = sentinel->next;
    const auto wanted = salted_hash ? hash ^ 0x92ca5df1 : hash;
    LoaderModule* found = nullptr;
    if (lock_loader)
        environment.acquire_peb_lock();
    while (current != sentinel) {
        auto* module = reinterpret_cast<LoaderModule*>(current);
        const auto actual =
            loaded_module_name_hash(module->base_name.buffer, module->base_name.length >> 1);
        if ((actual ^ 0x92ca5df1) == wanted) {
            found = module;
            break;
        }
        current = current->next;
    }
    if (lock_loader)
        environment.release_peb_lock();
    return found ? found->base : nullptr;
}

LoadedModuleEnvironment native_loaded_module_environment() {
#if defined(_WIN32)
#if defined(_M_IX86) || defined(__i386__)
    const auto peb = static_cast<std::uintptr_t>(__readfsdword(0x30));
    constexpr auto loader_offset = 0xc;
#else
    const auto peb = static_cast<std::uintptr_t>(__readgsqword(0x60));
    constexpr auto loader_offset = 0x18;
#endif
    LoaderData* loader;
    std::memcpy(&loader, reinterpret_cast<const std::byte*>(peb) + loader_offset, sizeof(loader));
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto acquire = GetProcAddress(native, "RtlAcquirePebLock");
    const auto release = GetProcAddress(native, "RtlReleasePebLock");
    if (!acquire || !release)
        throw std::runtime_error("Native PEB lock export is missing");
    return {loader, reinterpret_cast<PebLockFunction>(acquire),
            reinterpret_cast<PebLockFunction>(release)};
#else
    throw std::runtime_error("Native loaded-module enumeration requires Windows");
#endif
}

void* find_loaded_module(std::uint32_t hash, bool salted_hash, bool lock_loader) {
    return find_loaded_module(native_loaded_module_environment(), hash, salted_hash, lock_loader);
}
} // namespace skeet::engine
