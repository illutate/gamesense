#include "skeet/network/client_state_cvars.hpp"
#include "skeet/engine/executable_pattern.hpp"
#include <array>
#include <cstring>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
struct CvarArguments {
    const char* name;
    const char* value;
    const ServerCvarEventNames& names;
};
int marshal_cvar(script::Bridge& bridge, void* pointer) { //complete4339C702
    const auto& args = *static_cast<const CvarArguments*>(pointer);
    const auto& api = *bridge.api;
    auto* const state = bridge.state;
    api.createtable(state, 0, 2);
    api.pushlstring(state, args.names.name.get(), 8);
    api.pushstring(state, args.name);
    api.settable(state, -3);
    api.pushlstring(state, args.names.value.get(), 9);
    api.pushstring(state, args.value);
    api.settable(state, -3);
    return 1;
}
}
void observe_server_cvars(ClientStateServerCvars& runtime, const void* message) {
    if (!runtime.bridge.state || !(field<std::uint8_t>(message, 0x10) & 1) ||
        !field<void*>(message, 8))
        return;
    if (!runtime.lookup_attempted && !runtime.name_getter) {
        runtime.lookup_attempted = 1;
        constexpr std::array<std::uint8_t, 6> pattern{0xF6, 0x41, 0x18, 0x01, 0x74, 0x10};
        runtime.name_getter = reinterpret_cast<ServerCvarNameGetter>(
            engine::executable_pattern(runtime.engine_module, pattern.data(), pattern.size()));
    }
    if (!runtime.name_getter)
        return;
    auto* const list = field<const void*>(message, 8);
    const auto count = field<std::int32_t>(list, 0xC);
    for (std::int32_t index = 0; index < count; ++index) {
        auto* entry =
            field<void*>(field<const void*>(list, 8), std::ptrdiff_t(index) * sizeof(void*));
        const auto* name = runtime.name_getter(entry);
        const void* value = nullptr;
        if (field<std::uint32_t>(entry, 0x18) & 2) {
            auto* alternate = field<const void*>(entry, 0xC);
            if (field<std::uint32_t>(alternate, 0x10))
                value = alternate;
        }
        if (!value)
            value = field<const void*>(entry, 8);
        const auto* bytes = field<std::uint32_t>(value, 0x14) < 16 ? static_cast<const char*>(value)
                                                                   : field<const char*>(value, 0);
        if (!name || !*name || !bytes || !*bytes)
            return;
        CvarArguments args{name, bytes, runtime.names};
        script::dispatch_callbacks(runtime.bridge, runtime.names.event.get(), marshal_cvar, &args);
        // The actual4347CA4C slot is reread on the next iteration.
    }
}
} //namespace skeet::network
