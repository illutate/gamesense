#include "skeet/effects/scope_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SCOPE_MEMBER __thiscall
#define SCOPE_FAST __fastcall
#else
#define SCOPE_MEMBER
#define SCOPE_FAST
#endif
using OwnerEntry = void(SCOPE_MEMBER*)(void*);
struct ScopeOwner;
struct ScopeHud : hud::HudFrameCallback {
    ScopeOwner* owner;
};
void SCOPE_MEMBER destroy(void*);
void SCOPE_MEMBER empty(void*) {} //433CCE8E
void SCOPE_MEMBER discover(void*);
void SCOPE_FAST draw(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> primary_table{destroy, empty, empty,    empty, empty,
                                              empty,   empty, discover, empty}; //434727D8
const std::array<hud::HudFrameEntry, 1> hud_table{draw}; //43470428
struct ScopeOwner {
    const OwnerEntry* vtable = nullptr;
    std::uint32_t untouched_4 = 0;
    ScopeHud hud{{nullptr}, this};
    ScopeViewObserver updates;
    ScopeOwnerBindings bindings;
    engine::JobMemory memory;
    ScopeOwner(const ScopeOwnerRegistries& registry, ConfigRegistries& config,
               ScopeOwnerBindings context)
        : updates(context.runtime), bindings(context), memory(registry.lifecycle.memory) {
        registry.lifecycle.append(this);
        hud::register_hud_frame(registry.hud, &hud);
        network::register_update_observer(registry.updates, updates.endpoint);
        vtable = primary_table.data();
        hud.vtable = hud_table.data();
        updates.initialize_vtable();
        initialize_boolean_config(config.descriptors, bindings.settings.remove_scope, 0x171EED00u);
        bindings.runtime.scope_panel = -1;
    }
};
void SCOPE_MEMBER destroy(void* object) { //whole433B1A7A ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<ScopeOwner*>(object);
    const auto memory = owner->memory;
    owner->~ScopeOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void SCOPE_MEMBER discover(void* object) {
    static_cast<ScopeOwner*>(object)->bindings.runtime.initialize_scope_panel();
}
void SCOPE_FAST draw(void* endpoint, ui::RenderQueue* queue, void* player, std::int32_t*) {
    auto& bindings =
        static_cast<ScopeHud*>(static_cast<hud::HudFrameCallback*>(endpoint))->owner->bindings;
    bindings.runtime.draw_scope_live(*queue, player);
}
#undef SCOPE_MEMBER
#undef SCOPE_FAST
}
std::size_t scope_owner_size() noexcept {
    return sizeof(ScopeOwner);
}
void construct_scope_owner(void* storage, const ScopeOwnerRegistries& registry,
                           ConfigRegistries& config, ScopeOwnerBindings bindings) {
    ::new (storage) ScopeOwner(registry, config, bindings);
}
} // namespace skeet::effects
