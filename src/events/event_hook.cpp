#include "skeet/events/event_hook.hpp"

namespace skeet::events {
namespace {
EventPacketHookRuntime* active_runtime = nullptr;
}

EventPacketHookRuntime::EventPacketHookRuntime(EventRegistry& registry,
                                               engine::OwnerRegistry& observers,
                                               void* const* local_player) noexcept
    : bindings_{registry.engine_interface, registry.event_manager, local_player},
      master_(registry, observers, bindings_) {}

bool EventPacketHookRuntime::process(void* receiver, void* packet) {
    return master_.process(receiver, packet);
}

void bind_event_packet_hook_runtime(EventPacketHookRuntime& runtime) noexcept {
    active_runtime = &runtime;
}

engine::VtableHookNode* install_event_packet_hook(EventPacketHookRuntime& runtime,
                                                  engine::VtableHookRegistry& hooks, void* receiver,
                                                  engine::VtableHookMetadata& metadata) {
    // Publish the dispatcher before4331E834 swaps the receiver's table. Native
    // can install the table even when allocation of its cleanup node fails.
    bind_event_packet_hook_runtime(runtime);
    metadata.replacement_table[event_packet_slot] = reinterpret_cast<void*>(&event_packet_hook);
    return engine::install_vtable_hook(hooks, receiver, metadata);
}

#if defined(_M_IX86) || defined(__i386__)
bool __fastcall event_packet_hook(void* receiver, void*, void* packet) {
#else
bool event_packet_hook(void* receiver, void*, void* packet) {
#endif
    // Native433AE68A receives ECX=receiver and one stack argument (RET4).
    // The spare fastcall EDX parameter preserves that exact x86 entry ABI.
    return active_runtime->process(receiver, packet);
}
} // namespace skeet::events
