#include "skeet/effects/smoke_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SMOKE_MEMBER __thiscall
#define SMOKE_FAST __fastcall
#else
#define SMOKE_MEMBER
#define SMOKE_FAST
#endif
using OwnerEntry = void(SMOKE_MEMBER*)(void*);
WorldRuntime** active_slot = nullptr;
struct SmokeOwner;
struct SmokeObserver : network::UpdateObserver {
    SmokeOwner* owner;
};
void SMOKE_MEMBER destroy(void*);
void SMOKE_MEMBER empty(void*) {} //433CCE8E
void SMOKE_MEMBER before(void*);
void SMOKE_MEMBER after(void*);
void SMOKE_FAST player_view_empty(void*, void*, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
} //4337D653
void SMOKE_FAST view_empty(void*, float*, float*, float*) {} //4334A269
void SMOKE_FAST post_empty(void*, void*) {}
const std::array<OwnerEntry, 9> primary_table{destroy, empty, empty, empty, empty,
                                              empty,   empty, empty, empty}; //43470980
const network::UpdateObserverVtable update_table{
    {before, empty, empty, after, empty}, player_view_empty, view_empty, post_empty}; //4347005C
struct SmokeOwner {
    const OwnerEntry* vtable = nullptr;
    std::uint32_t untouched_4 = 0;
    SmokeObserver updates{{nullptr}, this};
    SmokeOwnerBindings bindings;
    engine::JobMemory memory;
    SmokeOwner(const SmokeOwnerRegistries& registry, ConfigRegistries& config,
               SmokeOwnerBindings context)
        : bindings(context), memory(registry.lifecycle.memory) {
        registry.lifecycle.append(this);
        network::register_update_observer(registry.updates, updates);
        vtable = primary_table.data();
        updates.vtable = &update_table;
        initialize_boolean_config(config.descriptors, bindings.runtime.settings.remove_smoke,
                                  0x623BD4F2u);
    }
};
void SMOKE_MEMBER before(void* object) {
    const auto& bindings =
        static_cast<SmokeObserver*>(static_cast<network::UpdateObserver*>(object))->owner->bindings;
    bindings.runtime.smoke_before_render(bindings.engine);
}
void SMOKE_MEMBER after(void* object) {
    const auto& bindings =
        static_cast<SmokeObserver*>(static_cast<network::UpdateObserver*>(object))->owner->bindings;
    bindings.runtime.smoke_after_render(bindings.engine);
}
void SMOKE_MEMBER destroy(void* object) { //whole433F216C ->433837D0
    *active_slot = nullptr;
    if (!object)
        return;
    auto* owner = static_cast<SmokeOwner*>(object);
    const auto memory = owner->memory;
    owner->~SmokeOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef SMOKE_MEMBER
#undef SMOKE_FAST
}
std::size_t smoke_owner_size() noexcept {
    return sizeof(SmokeOwner);
}
void construct_smoke_owner(void* storage, const SmokeOwnerRegistries& registry,
                           ConfigRegistries& config, SmokeOwnerBindings bindings) {
    ::new (storage) SmokeOwner(registry, config, bindings);
    active_slot = &bindings.active;
    bindings.active = &bindings.runtime;
}
} // namespace skeet::effects
