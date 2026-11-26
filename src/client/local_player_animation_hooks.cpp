#include "skeet/client/local_player_animation_hooks.hpp"
#include <intrin.h>
#include <stdexcept>

namespace skeet::client {
namespace {
LocalPlayerAnimationHooks* active;
void** original(void* player) noexcept {
    const auto* metadata =
        static_cast<const engine::VtableHookMetadata*>((*static_cast<void***>(player))[-2]);
    return metadata->original_table;
}
#if defined(_M_IX86) || defined(__i386__)
#define LOCAL_PLAYER_FASTCALL __fastcall
#else
#define LOCAL_PLAYER_FASTCALL
#endif
std::uint32_t LOCAL_PLAYER_FASTCALL strafe_entry(void* player, void*, std::uint32_t first,
                                                 std::uint32_t second) {
    return active->update_strafe(player, first, second,
                                 reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}
void LOCAL_PLAYER_FASTCALL animation_entry(void* player, void*) {
    active->update_client_animation(player);
}
#undef LOCAL_PLAYER_FASTCALL
}
std::uintptr_t LocalPlayerAnimationHooks::network_fix(void* player, void* supplied_object,
                                                      std::uintptr_t caller) {
    return misc::process_network_fix(player, supplied_object, caller, bindings.network_fix);
}
void LocalPlayerAnimationHooks::crosshair_view(void* player, void* view_setup,
                                               std::uintptr_t caller) {
    hud::prepare_crosshair_view(bindings.crosshair_config, bindings.crosshair, player, view_setup,
                                bindings.suspended != 0, reinterpret_cast<const std::byte*>(caller),
                                original(player));
}
// The original veneers copy the caller's nonvolatile register before their
// tail jump. Normal C++ prologues cannot reconstruct those hidden inputs.
extern "C" std::uintptr_t __cdecl
skeet_local_network_fix_bridge(void* player, void* supplied_object, std::uintptr_t caller) {
    return active->network_fix(player, supplied_object, caller);
}
extern "C" void __cdecl skeet_local_crosshair_view_bridge(void* player, void* view_setup,
                                                          std::uintptr_t caller) {
    active->crosshair_view(player, view_setup, caller);
}
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
extern "C" __attribute__((naked)) void skeet_local_network_fix_entry() {
    __asm__("pushl (%esp)\n\t"
            "pushl %ebp\n\t"
            "pushl %ecx\n\t"
            "call _skeet_local_network_fix_bridge\n\t"
            "addl $12, %esp\n\t"
            "ret");
}
extern "C" __attribute__((naked)) void skeet_local_crosshair_view_entry() {
    __asm__("pushl (%esp)\n\t"
            "pushl %edi\n\t"
            "pushl %ecx\n\t"
            "call _skeet_local_crosshair_view_bridge\n\t"
            "addl $12, %esp\n\t"
            "ret");
}
#elif defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void skeet_local_network_fix_entry() {
    __asm {
        push dword ptr [esp]
        push ebp
        push ecx
        call skeet_local_network_fix_bridge
        add esp,12
        ret
    }
}
extern "C" __declspec(naked) void skeet_local_crosshair_view_entry() {
    __asm {
        push dword ptr [esp]
        push edi
        push ecx
        call skeet_local_crosshair_view_bridge
        add esp,12
        ret
    }
}
#endif
void* LocalPlayerAnimationHooks::native_replacement_entry(std::size_t slot, std::uint32_t entry) {
    void* replacement;
    if (slot == 222 && entry == 0x43341FA7U)
        replacement = reinterpret_cast<void*>(strafe_entry);
    else if (slot == 224 && entry == 0x43329436U)
        replacement = reinterpret_cast<void*>(animation_entry);
#if defined(_M_IX86) || defined(__i386__)
    else if (slot == 268 && entry == 0x433C6689U)
        replacement = reinterpret_cast<void*>(skeet_local_network_fix_entry);
    else if (slot == 294 && entry == 0x433E009AU)
        replacement = reinterpret_cast<void*>(skeet_local_crosshair_view_entry);
#else
    else if ((slot == 268 && entry == 0x433C6689U) || (slot == 294 && entry == 0x433E009AU))
        throw std::logic_error("Local player EBP/EDI veneers require the native x86 ABI");
#endif
    else
        throw std::invalid_argument("Unknown local player animation hook slot/entry");
    return replacement;
}
void LocalPlayerAnimationHooks::bind_native_replacement(engine::VtableHookMetadata& metadata,
                                                        std::size_t slot, std::uint32_t entry) {
    auto* replacement = native_replacement_entry(slot, entry);
    active = this;
    metadata.replacement_table[slot] = replacement;
}
} // namespace skeet::client
