#include "skeet/engine/startup_operations.hpp"
#include "skeet/engine/executable_pattern.hpp"
#include "skeet/engine/loaded_modules.hpp"
#include "skeet/engine/unload.hpp"
#include <array>
#include <bit>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::engine {
// Devirtualized from VM entry 0x4354DB7E: LOCK BTR on x86 PEB + 0x28, bit 2.
void clear_startup_process_flag(volatile std::uint32_t& process_flags) noexcept {
    static_assert(sizeof(long) == sizeof(process_flags));
    InterlockedAnd(reinterpret_cast<volatile long*>(&process_flags), ~4L);
}

// Devirtualized from VM entry 0x4354DB7E: select an FF 23 match using RDTSC.
// The caller publishes the result to the native jump-target slot 0x43476448.
std::byte* find_startup_jump_target(std::byte* client_module, std::uint32_t timestamp_low) {
    constexpr std::array<std::uint8_t, 2> pattern{0xff, 0x23};
    const auto occurrence = (timestamp_low >> 3) & 31u;
    return executable_pattern(client_module, pattern.data(), 2, occurrence, 0xcc);
}

// Devirtualized from VM entry 0x4354DB7E: initialize the 0x43476B2C sentinel.
// Preserve the third field and the original next-then-previous store order.
void initialize_startup_hook_list(VtableHookNode& sentinel) noexcept {
    sentinel.next = &sentinel;
    sentinel.previous = &sentinel;
}

// Devirtualized from VM entry 0x4354DB7E: subtract in 64 bits, then divide signed.
// The subtraction wraps before interpretation as a signed value.
std::int64_t startup_unix_seconds(std::uint64_t file_time) noexcept {
    constexpr std::uint64_t epoch_offset = 116444736000000000ULL;
    return std::bit_cast<std::int64_t>(file_time - epoch_offset) / 10000000LL;
}

// Devirtualized from VM entry 0x4354DB7E: atomically consume 0x4346D4CC.
// Its initial value is the salted comctl32.dll hash; subsequent calls consume
// the current value, including zero. The native lookup requests the PEB lock.
std::byte* take_startup_common_controls(volatile std::uint32_t& module_hash,
                                        const LoadedModuleEnvironment& modules) {
    static_assert(sizeof(long) == sizeof(module_hash));
    const auto hash = static_cast<std::uint32_t>(
        InterlockedExchange(reinterpret_cast<volatile long*>(&module_hash), 0));
    return static_cast<std::byte*>(find_loaded_module(modules, hash, true, true));
}
}
