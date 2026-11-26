#include "skeet/network/tick_update_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include <bit>
#include <cstring>

namespace skeet::network {
namespace {
TickUpdateRuntime* runtime = nullptr;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
void** original_table(void* object) {
    const auto table = *static_cast<void***>(object);
    return static_cast<const engine::VtableHookMetadata*>(table[-2])->original_table;
}
bool retain_incoming_tick(const TickUpdateBindings& bindings) { //433240A6
    auto* holder = bindings.game_rules;
    if (!holder || !*holder)
        return true;
    const auto offset = std::ptrdiff_t(bindings.game_rule_guard);
    return field<std::uint8_t>(*holder, offset) != 0;
}
}

void update_tick_status(const void* status, TickUpdateGlobals& globals,
                        const TickUpdateBindings& bindings) { //433841D8
    if ((field<std::uint32_t>(status, 0x24) & 4u) && !field<std::uint8_t>(status, 0x10)) {
        if (*globals.sequence == -1)
            globals.transition_status = 0;
        else if (globals.transition_status == 1) {
            globals.transition_status = 2;
            globals.previous_processed_command =
                engine::invoke<std::int32_t>(bindings.engine_client, 205);
            // The engine can replace the pointer during virtual205; native
            // code reloads434758D8 for the write after that call.
            *const_cast<std::int32_t*>(globals.sequence) = -1;
        }
    } else {
        if (globals.transition_status == 2)
            globals.transition_status = 0;
        else {
            const auto delta = globals.acknowledged_at - *globals.current_time;
            const auto absolute =
                std::bit_cast<float>(std::bit_cast<std::uint32_t>(delta) & 0x7FFFFFFFu);
            if (absolute > 2.0f)
                globals.transition_status = 0;
        }
    }
    globals.previous_command = engine::invoke<std::int32_t>(bindings.engine_client, 205);
}
void publish_update_status(void* object, const void* status, TickUpdateGlobals& globals,
                           const TickUpdateBindings& bindings) { //4333CA14
    update_tick_status(status, globals, bindings);
    globals.receivers.update_status(status);
    using Original = engine::MemberFunction<void, const void*>;
    reinterpret_cast<Original>(original_table(object)[37])(object, status);
}
void publish_incoming_tick(void* object, std::uintptr_t first, std::int32_t incoming,
                           TickUpdateGlobals& globals,
                           const TickUpdateBindings& bindings) { //4341EFED
    const auto selected =
        retain_incoming_tick(bindings)
            ? incoming
            : misc::acknowledge_datagram_sequence(bindings.acknowledgements, incoming);
    globals.previous_command = -1;
    globals.interpolation =
        misc::query_channel_interpolation(bindings.engine_client, bindings.latency);
    globals.receivers.reset_update_state(selected);
    for (auto* patch = globals.patches; patch; patch = patch->next) {
        if (patch->enabled)
            bindings.exchange(patch->target, std::bit_cast<std::int32_t>(patch->enabled));
    }
    using Original = engine::MemberFunction<void, std::uintptr_t, std::int32_t>;
    reinterpret_cast<Original>(original_table(object)[5])(object, first, selected);
}
void bind_tick_update_runtime(TickUpdateRuntime& value) noexcept {
    runtime = &value;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall update_status_hook(void* object, void*, const void* status) {
#else
void update_status_hook(void* object, void*, const void* status) {
#endif
    runtime->status(object, status);
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall incoming_tick_hook(void* object, void*, std::uintptr_t first,
                                   std::int32_t incoming) {
#else
void incoming_tick_hook(void* object, void*, std::uintptr_t first, std::int32_t incoming) {
#endif
    runtime->incoming(object, first, incoming);
}
} //namespace skeet::network
