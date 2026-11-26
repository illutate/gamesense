#include "skeet/client/shutdown.hpp"
#include "skeet/client/commands.hpp"
#include <intrin.h>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {
void terminate_exit_callback_overflow() {
    using NativeTerminate = std::int32_t(__stdcall*)(void*, std::int32_t);
    const auto native = GetModuleHandleA("ntdll.dll");
    const auto address = native ? GetProcAddress(native, "ZwTerminateProcess") : nullptr;
    if (!address)
        throw std::runtime_error("Required ntdll!ZwTerminateProcess export is missing");
    (void)reinterpret_cast<NativeTerminate>(address)(reinterpret_cast<void*>(~std::uintptr_t(0)),
                                                     -3);
}
}
int ExitCallbacks::append(void (*callback)()) { // 0x433837F5
    if (entries.size() != 0x200)
        throw std::invalid_argument("Exit registration requires its512-slot backing store");
    if (!count)
        xor_mask = static_cast<std::uint32_t>(__rdtsc());
    else if (count == 0x200) {
        terminate_exit_callback_overflow();
        return -1;
    }
    const auto encoded = reinterpret_cast<std::uintptr_t>(callback) ^ xor_mask;
    for (std::uint32_t index = 0; index < count; ++index)
        if (entries[index] == encoded)
            return 0;
    entries[count] = encoded;
    ++count;
    return 0;
}
void ExitCallbacks::run() { // 0x433B3987
    const auto mask = xor_mask;
    while (count != 0) {
        const auto index = --count;
        entries[index] ^= mask;
        reinterpret_cast<void (*)()>(entries[index])();
        entries[index] = 0;
    }
    count = UINT32_MAX;
}
void ShutdownRuntime::restore_engine_dispatch() { // 0x43365AA1
    if (source_channel_) {
        if (!source_channel_->replacement_table)
            return;
        auto* const channel = engine::invoke<void*>(input.interfaces.engine_client, 78);
        if (channel && *static_cast<void***>(channel) == source_channel_->replacement_table)
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(channel),
                                       source_channel_->expected_table);
        return;
    }
    if (!installed_engine_dispatch)
        return;
    auto* slot = engine::invoke<std::uint32_t*>(input.interfaces.engine_client, 78);
    if (slot && *slot == installed_engine_dispatch)
        InterlockedExchange(reinterpret_cast<volatile LONG*>(slot),
                            static_cast<LONG>(saved_engine_dispatch));
}
void ShutdownRuntime::release_cleanup_objects() { // 0x4339D451
    const auto objects = owners.entries();
    for (auto* object : objects)
        engine::invoke<void>(object, 0);
}
void ShutdownRuntime::cancel_timer() { // 0x433D7307
    if (!native_window)
        throw std::invalid_argument("Client shutdown main window is unresolved");
    (void)imports.kill_timer(native_window, 0x332);
}
}
