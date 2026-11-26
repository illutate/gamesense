#include "skeet/events/hit_marker_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::events {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define HIT_MEMBER __thiscall
#define HIT_FAST __fastcall
#else
#define HIT_MEMBER
#define HIT_FAST
#endif
using OwnerEntry = void(HIT_MEMBER*)(void*);
struct HudEndpoint : hud::HudFrameCallback {
    HitMarkerLifecycleOwner* owner;
};
void HIT_MEMBER destroy_hit_marker(void*);
void HIT_MEMBER reset_hit_marker(void*);
void HIT_MEMBER forward_hit_marker_reset(void*);
void HIT_MEMBER unchanged_hit_marker(void*) {} //433CCE8E RET
void HIT_FAST draw_hit_marker(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> lifecycle_table{
    destroy_hit_marker,   unchanged_hit_marker,     unchanged_hit_marker,
    unchanged_hit_marker, unchanged_hit_marker,     reset_hit_marker,
    unchanged_hit_marker, forward_hit_marker_reset, unchanged_hit_marker};
const std::array<hud::HudFrameEntry, 1> hud_table{draw_hit_marker};
}

class HitMarkerLifecycleOwner {
public:
    const OwnerEntry* vtable = nullptr;
    HudEndpoint hud_endpoint{{nullptr}, this};
    void* captured_surface; //native+C, written once by43486543..43486549
    engine::JobMemory memory;
    HitMarkerOwnerBindings bindings;
    HitMarker state;
    HitMarkerListener listener;

    HitMarkerLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                            engine::OwnerRegistryBinding hud_registry, ConfigRegistries& registries,
                            EventRegistry& events, HitMarkerOwnerBindings source)
        : memory(lifecycle.memory), bindings(source),
          state(source.settings, source.remaining, source.sound), listener{state, source.events} {
        lifecycle.append(this); //43486524 ->433676A0, primary still null
        hud::register_hud_frame(hud_registry, &hud_endpoint); //4348652C ->433D20B5
        vtable = lifecycle_table.data(); //4347253C
        hud_endpoint.vtable = hud_table.data(); //4346F6A8
        captured_surface = bindings.events.surface; //mem_int3.h:575,43486543
        esp::initialize_hit_marker_config(registries.descriptors, bindings.settings);
        bindings.remaining = 0.0f; //43486575
        misc::construct_native_text(bindings.sound, "buttons/arena_switch_press_02.wav", memory);
        listener.bind(events); //4348661D ->4336BAAD,89FA8001 /433BFDF6
    }
};

namespace {
void HIT_FAST draw_hit_marker(void* endpoint, ui::RenderQueue* queue, void* player, std::int32_t*) {
    auto& owner = *static_cast<HudEndpoint*>(static_cast<hud::HudFrameCallback*>(endpoint))->owner;
    owner.state.draw_live(*queue, player, owner.bindings.global_variables,
                          owner.bindings.rejection_byte);
}
void HIT_MEMBER reset_hit_marker(void* object) { //4336897C
    static_cast<HitMarkerLifecycleOwner*>(object)->state.reset();
}
void HIT_MEMBER forward_hit_marker_reset(void* object) { //433CF2F5
    const auto* owner = static_cast<HitMarkerLifecycleOwner*>(object);
    owner->vtable[5](object);
}
void HIT_MEMBER destroy_hit_marker(void* object) { //433FA5C7
    if (!object)
        return;
    auto* owner = static_cast<HitMarkerLifecycleOwner*>(object);
    misc::destroy_native_text(owner->bindings.sound, owner->memory); //4336B1E9, first
    // The original primary does not unregister the event/HUD entries. Their
    // common shutdown owns the ranges; no individual listener removal is added.
    const auto memory = owner->memory;
    owner->~HitMarkerLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement); //433837D0
}
}
std::size_t hit_marker_owner_size() noexcept {
    return sizeof(HitMarkerLifecycleOwner);
}
HitMarkerLifecycleOwner& construct_hit_marker(void* storage, engine::OwnerRegistryBinding lifecycle,
                                              engine::OwnerRegistryBinding hud_registry,
                                              ConfigRegistries& registries, EventRegistry& events,
                                              HitMarkerOwnerBindings bindings) {
    return *::new (storage)
        HitMarkerLifecycleOwner(lifecycle, hud_registry, registries, events, bindings);
}
HitMarker& hit_marker_runtime(HitMarkerLifecycleOwner& owner) noexcept {
    return owner.state;
}
#undef HIT_MEMBER
#undef HIT_FAST
} // namespace skeet::events
