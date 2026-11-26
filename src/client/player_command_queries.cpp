#include "skeet/client/player_command_queries.hpp"
#include "skeet/client/input_commands.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>
#if defined(_MSC_VER)
#include <intrin.h>
#define SKEET_PLAYER_QUERY_NOINLINE __declspec(noinline)
#define SKEET_PLAYER_QUERY_CALLER _ReturnAddress()
#else
#define SKEET_PLAYER_QUERY_NOINLINE [[gnu::noinline]]
#define SKEET_PLAYER_QUERY_CALLER __builtin_return_address(0)
#endif
#if defined(__GNUC__) || defined(__clang__)
#define SKEET_PLAYER_QUERY_FORCEINLINE [[gnu::always_inline]] inline
#define SKEET_PLAYER_QUERY_ASM_BRIDGE [[gnu::used, gnu::noinline]]
#elif defined(_MSC_VER)
#define SKEET_PLAYER_QUERY_FORCEINLINE __forceinline
// The MSVC inline assembler retains its direct symbol reference; GNU's string
// assembler needs the explicit used attribute above for this same bridge.
#define SKEET_PLAYER_QUERY_ASM_BRIDGE __declspec(noinline)
#endif

namespace skeet::client {
namespace {
PlayerCommandQueries* active_queries;

template <class T>
SKEET_PLAYER_QUERY_FORCEINLINE T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class Result, class... Arguments>
Result original(void* receiver, std::size_t slot, Arguments... arguments) {
    auto** table = *static_cast<void***>(receiver);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    return reinterpret_cast<engine::MemberFunction<Result, Arguments...>>(
        metadata->original_table[slot])(receiver, arguments...);
}
}

void PlayerCommandQueries::dispatch_update_call(void* player,
                                                std::array<std::uint32_t, 4> args) const {
    if (bindings.update_phase == 1u)
        return;
    original<void>(player, 78, args[0], args[1], args[2], args[3]);
}

std::uint32_t PlayerCommandQueries::query_callsite(void* player, std::uint32_t caller_esi,
                                                   std::uintptr_t return_address,
                                                   std::uintptr_t stack_word_08) const {
    if (stack_word_08 == bindings.forced_query_stack)
        return 1u;
    if (bindings.team_query_override && !bindings.update_mode &&
        return_address == bindings.team_query_return) {
        if (read<std::int32_t>(player, bindings.team) == 3 ||
            read<std::int32_t>(player, bindings.team) == 2)
            return caller_esi;
    }
    return original<std::uint32_t>(player, 88);
}

std::uint8_t PlayerCommandQueries::interpolate(void* player, std::uint32_t time_bits) const {
    if (bindings.interpolation_guard)
        *bindings.interpolation_guard = 0;
    if (!engine::invoke<std::uint8_t>(bindings.engine_client, 82) &&
        !engine::invoke<std::uint8_t>(bindings.engine_client, 93) &&
        !read<std::uint8_t>(player, bindings.rejection_byte)) {
        const auto number = std::bit_cast<std::int32_t>(std::uint32_t(*bindings.choked_commands) +
                                                        std::uint32_t(*bindings.last_outgoing));
        const auto* command = find_input_command(bindings.input, number);
        if (command && command->tick == 0x7F7FFFFF)
            return 1;
    }
    // Float occupies one stack word on the native ABI. Forwarding its bits
    // avoids an extra x87 load/store that could alter a signaling NaN.
    return original<std::uint8_t>(player, 113, time_bits);
}

std::array<float, 3>* PlayerCommandQueries::eye_angles(void* player,
                                                       std::uintptr_t return_address) const {
    if (return_address == bindings.sent_angles_return && !bindings.update_mode)
        return &bindings.sent_history.angles;
    return original<std::array<float, 3>*>(player, 170);
}

// Slot88 has no ordinary stack arguments. Its native thunk copies incoming
// ESI into EDX and its callee inspects [entryESP] and [entryESP+8]. Capture
// all three words before a C++ prologue can use ESI or change ESP.
extern "C" SKEET_PLAYER_QUERY_ASM_BRIDGE std::uint32_t __cdecl
skeet_player_query88_dispatch(void* player, std::uint32_t caller_esi, std::uintptr_t return_address,
                              std::uintptr_t stack_word_08) {
    return active_queries->query_callsite(player, caller_esi, return_address, stack_word_08);
}

#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define SKEET_PLAYER_QUERY_STRING_IMPL(value) #value
#define SKEET_PLAYER_QUERY_STRING(value) SKEET_PLAYER_QUERY_STRING_IMPL(value)
extern "C" __attribute__((naked, used)) std::uint32_t skeet_player_query88_entry() {
    __asm__ __volatile__(
        "pushl %ebp\n\t"
        "movl %esp, %ebp\n\t"
        "andl $-16, %esp\n\t"
        "pushl 12(%ebp)\n\t"
        "pushl 4(%ebp)\n\t"
        "pushl %esi\n\t"
        "pushl %ecx\n\t"
        "call " SKEET_PLAYER_QUERY_STRING(__USER_LABEL_PREFIX__) "skeet_player_query88_dispatch\n\t"
                                                                 "movl %ebp, %esp\n\t"
                                                                 "popl %ebp\n\t"
                                                                 "ret\n\t");
}
#undef SKEET_PLAYER_QUERY_STRING
#undef SKEET_PLAYER_QUERY_STRING_IMPL
#elif defined(_M_IX86) && defined(_MSC_VER)
extern "C" __declspec(naked) std::uint32_t skeet_player_query88_entry() {
    __asm {
        push ebp
        mov ebp,esp
        and esp,-16
        push dword ptr [ebp+12]
        push dword ptr [ebp+4]
        push esi
        push ecx
        call skeet_player_query88_dispatch
        mov esp,ebp
        pop ebp
        ret
    }
}
#endif

namespace {
#if defined(_M_IX86) || defined(__i386__)
void __fastcall update_call_entry(void* player, void*, std::uint32_t a, std::uint32_t b,
                                  std::uint32_t c, std::uint32_t d) {
    active_queries->dispatch_update_call(player, {a, b, c, d});
}
std::uint8_t __fastcall interpolation_entry(void* player, void*, float time) {
    return active_queries->interpolate(player, std::bit_cast<std::uint32_t>(time));
}
SKEET_PLAYER_QUERY_NOINLINE std::array<float, 3>* __fastcall eye_angles_entry(void* player, void*) {
    const auto caller = reinterpret_cast<std::uintptr_t>(SKEET_PLAYER_QUERY_CALLER);
    return active_queries->eye_angles(player, caller);
}
#else
void update_call_entry(void* player, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                       std::uint32_t d) {
    active_queries->dispatch_update_call(player, {a, b, c, d});
}
std::uint8_t interpolation_entry(void* player, float time) {
    return active_queries->interpolate(player, std::bit_cast<std::uint32_t>(time));
}
SKEET_PLAYER_QUERY_NOINLINE std::array<float, 3>* eye_angles_entry(void* player) {
    const auto caller = reinterpret_cast<std::uintptr_t>(SKEET_PLAYER_QUERY_CALLER);
    return active_queries->eye_angles(player, caller);
}
#endif
}

void* PlayerCommandQueries::native_replacement_entry(std::size_t slot, std::uint32_t entry) {
    void* replacement;
    switch (entry) {
    case 0x433842C1u:
        if (slot != 78)
            throw std::invalid_argument("Player update-call slot does not match native entry");
        replacement = reinterpret_cast<void*>(&update_call_entry);
        break;
    case 0x4334D3EBu:
        if (slot != 88)
            throw std::invalid_argument("Player callsite-query slot does not match native entry");
#if defined(_M_IX86) || defined(__i386__)
        replacement = reinterpret_cast<void*>(&skeet_player_query88_entry);
        break;
#else
        throw std::logic_error(
            "The player callsite-query entry requires the native x86 ESI/stack ABI");
#endif
    case 0x4340191Du:
        if (slot != 113)
            throw std::invalid_argument("Player interpolation slot does not match native entry");
        replacement = reinterpret_cast<void*>(&interpolation_entry);
        break;
    case 0x433E97C1u:
        if (slot != 170)
            throw std::invalid_argument("Player eye-angle slot does not match native entry");
        replacement = reinterpret_cast<void*>(&eye_angles_entry);
        break;
    default:
        throw std::invalid_argument("Unknown captured player command-query entry");
    }
    return replacement;
}
void PlayerCommandQueries::bind_native_replacement(engine::VtableHookMetadata& metadata,
                                                   std::size_t slot, std::uint32_t entry) {
    auto* replacement = native_replacement_entry(slot, entry);
    active_queries = this;
    metadata.replacement_table[slot] = replacement;
}
} // namespace skeet::client
#undef SKEET_PLAYER_QUERY_NOINLINE
#undef SKEET_PLAYER_QUERY_CALLER
#undef SKEET_PLAYER_QUERY_FORCEINLINE
#undef SKEET_PLAYER_QUERY_ASM_BRIDGE
