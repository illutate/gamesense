#include "skeet/network/client_state_hook_bindings.hpp"
#include "skeet/engine/unload.hpp"
#include <cstring>

namespace skeet::network {
namespace {
ClientStateHooksRuntime* runtime;
template <class Result, class... Args>
Result original(void* object, std::size_t slot, Args... args) {
    auto** table = *static_cast<void***>(object);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    return reinterpret_cast<engine::MemberFunction<Result, Args...>>(
        metadata->original_table[slot])(object, args...);
}
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
bool ClientStateHooksRuntime::slot2(void* object, std::uintptr_t incoming) {
    const bool result = original<bool>(object, 2, incoming);
    client::restore_channel_hook(bindings.engine_client, bindings.channel);
    return result;
}
std::uintptr_t ClientStateHooksRuntime::slot15(void* object, std::uintptr_t incoming) {
    observe_server_cvars(bindings.server_cvars, reinterpret_cast<const void*>(incoming));
    return original<std::uintptr_t>(object, 15, incoming);
}
std::uintptr_t ClientStateHooksRuntime::slot32(void* object, std::uintptr_t incoming) {
    const auto* message = reinterpret_cast<const void*>(incoming);
    const auto identifier =
        (field<std::uint8_t>(message, 0x18) & 1) ? field<std::int32_t>(message, 0xC) : -1;
    auto* patch = bindings.scoped_patches;
    while (patch && patch->identifier != identifier)
        patch = patch->next;
    if (patch && patch->target) {
        patch->saved = *patch->target;
        *patch->target = patch;
    }
    const auto result = original<std::uintptr_t>(object, 32, incoming);
    if (patch && patch->target)
        *patch->target = patch->saved;
    return result;
}
void bind_client_state_hooks_runtime(ClientStateHooksRuntime& value) noexcept {
    runtime = &value;
}
#if defined(_M_IX86) || defined(__i386__)
#define CLIENT_STATE_FAST __fastcall
#else
#define CLIENT_STATE_FAST
#endif
bool CLIENT_STATE_FAST client_state_slot2_hook(void* object, void*, std::uintptr_t incoming) {
    return runtime->slot2(object, incoming);
}
std::uintptr_t CLIENT_STATE_FAST client_state_slot12_hook(void* object, void*,
                                                          std::uintptr_t incoming) {
    return original<std::uintptr_t>(object, 12, incoming);
}
std::uintptr_t CLIENT_STATE_FAST client_state_slot15_hook(void* object, void*,
                                                          std::uintptr_t incoming) {
    return runtime->slot15(object, incoming);
}
std::uintptr_t CLIENT_STATE_FAST client_state_slot25_hook(void* object, void*,
                                                          std::uintptr_t incoming) {
    return original<std::uintptr_t>(object, 25, incoming);
}
std::uintptr_t CLIENT_STATE_FAST client_state_slot32_hook(void* object, void*,
                                                          std::uintptr_t incoming) {
    return runtime->slot32(object, incoming);
}
bool CLIENT_STATE_FAST client_state_slot47_hook(void* object, void*, std::int32_t state,
                                                std::uintptr_t second, std::uintptr_t third) {
    return runtime->slot47(object, state, second, third);
}
#undef CLIENT_STATE_FAST
} //namespace skeet::network
