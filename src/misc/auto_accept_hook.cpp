#include "skeet/misc/auto_accept_hook.hpp"
#include "skeet/misc/native_text.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/unload.hpp"
#include <array>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
using AcceptCallback = void(__stdcall*)(const char*);
#else
using AcceptCallback = void (*)(const char*);
#endif
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
const engine::VtableHookMetadata& metadata(void* receiver) {
    auto** table = field<void**>(receiver, 0);
    return *static_cast<const engine::VtableHookMetadata*>(table[-2]);
}
AutoAcceptPatchState* restore_patch = nullptr;
}

std::uint32_t process_auto_accept_event(void* receiver, const char* event_name,
                                        std::uintptr_t second, std::uintptr_t third,
                                        std::uintptr_t fourth, std::uintptr_t fifth,
                                        std::uintptr_t sixth, std::uintptr_t seventh) {
    if (event_name) {
        auto* owner = static_cast<AutoAcceptLifecycleOwner*>(metadata(receiver).owner);
        const auto data = auto_accept_handler_data(*owner);
        if (data.configuration.enabled.value &&
            engine::netvar_name_hash(event_name) == data.name_hash) {
            //43320459..67 captures both encrypted bytes/key and callback before
            // copying. The local buffer is the native pair of stack words.
            const NativeEncryptedText encoded{data.name.bytes, data.name.key, {}};
            const auto callback = reinterpret_cast<AcceptCallback>(data.callback);
            std::array<char, 8> decoded;
            decode_native_text(encoded, decoded.data());
            // One pushed string argument, no caller ADD ESP: the genuine
            // constructor callback4346D554 uses stdcall/RET4; result unused.
            callback(decoded.data());
        }
    }
    // The callback may change the receiver's table. Native reloads it and its
    // metadata AFTER the callback, then selects original byte offset4/slot1.
    auto** original = metadata(receiver).original_table;
    using Original =
        engine::MemberFunction<std::uint32_t, const char*, std::uintptr_t, std::uintptr_t,
                               std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t>;
    return reinterpret_cast<Original>(original[1])(receiver, event_name, second, third, fourth,
                                                   fifth, sixth, seventh);
}
#if defined(_M_IX86) || defined(__i386__)
std::uint32_t __fastcall auto_accept_event_hook(void* receiver, void*, const char* event_name,
#else
std::uint32_t auto_accept_event_hook(void* receiver, void*, const char* event_name,
#endif
                                                std::uintptr_t second, std::uintptr_t third,
                                                std::uintptr_t fourth, std::uintptr_t fifth,
                                                std::uintptr_t sixth, std::uintptr_t seventh) {
    return process_auto_accept_event(receiver, event_name, second, third, fourth, fifth, sixth,
                                     seventh);
}

std::uint32_t restore_auto_accept_receiver(AutoAcceptPatchState& patch, void* receiver,
                                           std::uintptr_t argument) {
    auto** original = metadata(receiver).original_table;
    using Entry = engine::MemberFunction<std::uint32_t, std::uintptr_t>;
    //4338A9E9 saves original slot0 before exchanging the receiver's table.
    const auto entry = reinterpret_cast<Entry>(original[0]);
    InterlockedExchangePointer(static_cast<void* volatile*>(receiver),
                               patch.metadata.original_table);
    patch.metadata.target = nullptr;
    return entry(receiver, argument);
}
void bind_auto_accept_restore_hook(AutoAcceptPatchState& patch) noexcept {
    restore_patch = &patch;
}
#if defined(_M_IX86) || defined(__i386__)
std::uint32_t __fastcall auto_accept_restore_hook(void* receiver, void*, std::uintptr_t argument) {
#else
std::uint32_t auto_accept_restore_hook(void* receiver, void*, std::uintptr_t argument) {
#endif
    return restore_auto_accept_receiver(*restore_patch, receiver, argument);
}
} //namespace skeet::misc
