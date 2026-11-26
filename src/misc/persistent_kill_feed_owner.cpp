#include "skeet/misc/persistent_kill_feed.hpp"
#include "skeet/events/event_dispatch.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define FEED_MEMBER __thiscall
#else
#define FEED_MEMBER
#endif
using OwnerEntry = void(FEED_MEMBER*)(void*);
struct KillFeedInstance;
void FEED_MEMBER destroy(void*);
void FEED_MEMBER unchanged(void*) {} //433CCE8E
void FEED_MEMBER release(void*);
void FEED_MEMBER reset_state(void*);
void FEED_MEMBER forward_reset(void*);
const std::array<OwnerEntry, 9> owner_table{destroy,   unchanged,     unchanged,
                                            unchanged, release,       reset_state,
                                            unchanged, forward_reset, unchanged};
struct KillFeedInstance {
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    NativeText hud_name;
    PersistentKillFeedRuntime runtime;
    alignas(events::KillFeedEventObserver)
        std::array<std::byte, sizeof(events::KillFeedEventObserver)> event_storage;
    alignas(PersistentKillFeedFrameHandler)
        std::array<std::byte, sizeof(PersistentKillFeedFrameHandler)> frame_storage;
    KillFeedInstance(engine::OwnerRegistryBinding owners,
                     engine::OwnerRegistryBinding event_receivers,
                     engine::OwnerRegistryBinding frame_receivers, ConfigRegistries& registry,
                     PersistentKillFeedConfig& config, PersistentKillFeedBindings& bindings)
        : memory(owners.memory), runtime(config, bindings, hud_name) {
        owners.append(this);
        auto* event = ::new (event_storage.data()) events::KillFeedEventObserver(runtime);
        events::register_event_observer(event_receivers.registry, *event, event_receivers.memory);
        ::new (frame_storage.data()) PersistentKillFeedFrameHandler(frame_receivers, runtime);
        vtable = owner_table.data();
        initialize_persistent_kill_feed_config(registry, config);
        runtime.pending_count = 0;
        construct_native_text(hud_name, "CCSGO_HudDeathNotice", memory);
    }
    ~KillFeedInstance() {
        destroy_native_text(hud_name, memory); //433D11D3 ->4336B1E9
        std::launder(reinterpret_cast<PersistentKillFeedFrameHandler*>(frame_storage.data()))
            ->~PersistentKillFeedFrameHandler();
        std::launder(reinterpret_cast<events::KillFeedEventObserver*>(event_storage.data()))
            ->~KillFeedEventObserver();
    }
};
void FEED_MEMBER destroy(void* object) { //433D11D3
    auto* owner = static_cast<KillFeedInstance*>(object);
    const auto memory = owner->memory;
    owner->~KillFeedInstance();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void FEED_MEMBER release(void* object) {
    static_cast<KillFeedInstance*>(object)->runtime.release();
}
void FEED_MEMBER reset_state(void* object) {
    static_cast<KillFeedInstance*>(object)->runtime.reset_state();
}
void FEED_MEMBER forward_reset(void* object) {
    static_cast<KillFeedInstance*>(object)->vtable[5](object);
}
#undef FEED_MEMBER
}
std::size_t persistent_kill_feed_owner_size() noexcept {
    return sizeof(KillFeedInstance);
}
PersistentKillFeedRuntime&
construct_persistent_kill_feed_owner(void* storage, engine::OwnerRegistryBinding owners,
                                     engine::OwnerRegistryBinding event_receivers,
                                     engine::OwnerRegistryBinding frame_receivers,
                                     ConfigRegistries& registry, PersistentKillFeedConfig& config,
                                     PersistentKillFeedBindings& bindings) {
    return (::new (storage) KillFeedInstance(owners, event_receivers, frame_receivers, registry,
                                             config, bindings))
        ->runtime;
}
} //namespace skeet::misc
