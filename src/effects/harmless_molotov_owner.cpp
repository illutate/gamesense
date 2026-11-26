#include "skeet/effects/harmless_molotov_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define MOLOTOV_MEMBER __thiscall
#define MOLOTOV_FAST __fastcall
#else
#define MOLOTOV_MEMBER
#define MOLOTOV_FAST
#endif
using OwnerEntry = void(MOLOTOV_MEMBER*)(void*);
void MOLOTOV_MEMBER destroy(void*);
void MOLOTOV_MEMBER empty(void*) {} //4347D93C/433CCE8E
void MOLOTOV_MEMBER phase_empty(void*) {}
void MOLOTOV_FAST player_view_empty(void*, void*, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
} //4337D653
void MOLOTOV_FAST view_empty(void*, float*, float*, float*) {} //4334A269
void MOLOTOV_FAST post_view_empty(void*, void*) {}
const std::array<OwnerEntry, 9> primary_table{destroy, empty, empty, empty, empty,
                                              empty,   empty, empty, empty}; //4346FB44
const network::UpdateObserverVtable update_table{
    {phase_empty, phase_empty, phase_empty, phase_empty, phase_empty},
    player_view_empty,
    view_empty,
    post_view_empty}; //434709A4
struct MolotovOwner {
    const OwnerEntry* vtable = nullptr;
    std::uint32_t untouched_4 = 0;
    network::UpdateObserver updates{nullptr};
    HarmlessMolotovOwnerBindings bindings;
    engine::JobMemory memory;
    HarmlessMolotovState state{false};
    HarmlessMolotovRuntime runtime;
    MolotovOwner(const HarmlessMolotovOwnerRegistries& registry, ConfigRegistries& config,
                 HarmlessMolotovOwnerBindings context)
        : bindings(context), memory(registry.lifecycle.memory),
          runtime{context.settings, state, context.effects} {
        registry.lifecycle.append(this);
        network::register_update_observer(registry.updates, updates);
        vtable = primary_table.data();
        updates.vtable = &update_table;
        auto& settings = bindings.settings;
        initialize_boolean_config(config.descriptors, settings.harmless_molotovs, 0xC82CBD3Bu);
        initialize_color_config(config.descriptors, settings.harmless_molotov_color, 0xF9200DEFu,
                                0xFFFF5800u);
        initialize_integer_config(config.descriptors, settings.molotov_percent, 0x921CAA0Au, 65);
        state.initialize_native_storage();
    }
};
void MOLOTOV_MEMBER destroy(void* object) { //whole433E51E8 ->43326010 ->433837D0
    unbind_harmless_molotov_runtime();
    if (!object)
        return;
    auto* owner = static_cast<MolotovOwner*>(object);
    owner->state.release_native_storage();
    const auto memory = owner->memory;
    owner->~MolotovOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef MOLOTOV_MEMBER
#undef MOLOTOV_FAST
}
std::size_t harmless_molotov_owner_size() noexcept {
    return sizeof(MolotovOwner);
}
void construct_harmless_molotov_owner(void* storage, const HarmlessMolotovOwnerRegistries& registry,
                                      ConfigRegistries& config,
                                      HarmlessMolotovOwnerBindings bindings) {
    auto* owner = ::new (storage) MolotovOwner(registry, config, bindings);
    owner->runtime.publish();
}
} // namespace skeet::effects
