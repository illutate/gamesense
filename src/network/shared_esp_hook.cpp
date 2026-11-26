#include "skeet/network/shared_esp.hpp"
#include "skeet/engine/unload.hpp"

namespace skeet::network {
namespace {
SharedEspRuntime* active_runtime = nullptr;
int marshal_voice(script::Bridge& bridge, void* context) { //433BA24E
    const auto& api = *bridge.api;
    api.createtable(bridge.state, 0, 0);
    api.pushstring(bridge.state, "data");
    api.pushlightuserdata(bridge.state, context);
    api.settable(bridge.state, -3);
    return 1;
}
}
void dispatch_voice_message(SharedEspRuntime& runtime, void* message) { //433E90B7
    if (!runtime.script.state)
        return;
    script::dispatch_callbacks(runtime.script, runtime.voice_event_name.get(), marshal_voice,
                               message);
}
bool process_voice_message(SharedEspRuntime& runtime, void* receiver, void* message) { //433FD2C4
    if (receive_shared_esp(runtime, message))
        return true;
    const auto table = *static_cast<void***>(receiver);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    using Original = engine::MemberFunction<bool, void*>;
    return reinterpret_cast<Original>(metadata->original_table[24])(receiver, message);
}
void bind_shared_esp_runtime(SharedEspRuntime& runtime) noexcept {
    active_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
bool __fastcall process_voice_message_hook(void* receiver, void*, void* message) {
#else
bool process_voice_message_hook(void* receiver, void*, void* message) {
#endif
    return process_voice_message(*active_runtime, receiver, message);
}
} // namespace skeet::network
