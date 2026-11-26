#include "skeet/engine/frame_update.hpp"
#include <bit>

namespace skeet::engine {
namespace {
FrameUpdateRuntime* frame_runtime;
}
FrameUpdateEndpoint* register_frame_update(OwnerRegistryBinding owners,
                                           FrameUpdateEndpoint* endpoint) {
    void* value =
        reinterpret_cast<std::uintptr_t>(endpoint) + std::uintptr_t{4} == 0 ? nullptr : endpoint;
    append_owner(owners.registry, &value, owners.memory);
    return endpoint;
}
void dispatch_frame_update(const OwnerRegistry& owners, std::int32_t& counter,
                           std::uint8_t update) {
    if (update) {
        auto next = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(counter) - 1u);
        if (next < 0)
            next = 0;
        counter = next;
    }
    const auto entries = owners.entries();
    for (auto* object : entries) {
        auto* endpoint = static_cast<FrameUpdateEndpoint*>(object);
        endpoint->vtable[0](object, update);
    }
}
void bind_frame_update_runtime(FrameUpdateRuntime& runtime) noexcept {
    frame_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
void __stdcall frame_update_hook(std::uint8_t update) {
#else
void frame_update_hook(std::uint8_t update) {
#endif
    dispatch_frame_update(frame_runtime->callbacks, frame_runtime->counter, update);
}
} //namespace skeet::engine
