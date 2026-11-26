#include "skeet/effects/free_look_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_FREE_OWNER_MEMBER __thiscall
#else
#define SKEET_FREE_OWNER_MEMBER
#endif
using PrimaryEntry = void(SKEET_FREE_OWNER_MEMBER*)(void*);
void SKEET_FREE_OWNER_MEMBER destroy_entry(void*);
void SKEET_FREE_OWNER_MEMBER reset_entry(void*);
void SKEET_FREE_OWNER_MEMBER forward_reset_entry(void*);
void SKEET_FREE_OWNER_MEMBER unchanged(void*) {} //433CCE8E
//Complete primary4346FA68;433CF2F5 forwards slot7 to the current slot5.
const std::array<PrimaryEntry, 9> primary_table{destroy_entry, unchanged,           unchanged,
                                                unchanged,     unchanged,           reset_entry,
                                                unchanged,     forward_reset_entry, unchanged};
}

class FreeLookOwner {
public:
    const PrimaryEntry* vtable = nullptr;
    std::uintptr_t preserved_second_word; //native+4 is not written by this ctor
    FreeLookViewObserver observer;
    FreeLookViewRuntime runtime;
    engine::JobMemory memory;
    FreeLookOwner** active;
    FreeLookOwnerState* canonical;

    FreeLookOwner(engine::OwnerRegistryBinding lifecycle, engine::OwnerRegistryBinding updates,
                  FreeLookOwnerBindings bindings)
        : observer(runtime, false),
          runtime(bindings.state.command, bindings.state.view, bindings.view),
          memory(lifecycle.memory), active(&bindings.active), canonical(&bindings.state) {
        lifecycle.append(this); //43484A3A ->433676A0, primary word still zero
        network::register_update_observer(updates, observer.endpoint); //owner+8 ->433B8C1C
        vtable = primary_table.data(); //43484A4E, native4346FA68
        observer.initialize_vtable(); //43484A57, native4347269C
        canonical->command.delta = {0.0f, 0.0f, 0.0f}; //43484A5E..43484A62 ONLY
    }
};
#if UINTPTR_MAX == UINT32_MAX
// Clang's x86 layout extension verifies this reference-bearing native prefix.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(FreeLookOwner, preserved_second_word) == 4);
static_assert(offsetof(FreeLookOwner, observer) == 8);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
static_assert(sizeof(FreeLookOwner) <= native_free_look_owner_allocation_size);
#endif

namespace {
void SKEET_FREE_OWNER_MEMBER destroy_entry(void* object) {
    auto* owner = static_cast<FreeLookOwner*>(object);
    destroy_free_look_owner(owner, *owner->active, owner->memory);
}
void SKEET_FREE_OWNER_MEMBER reset_entry(void* object) {
    static_cast<FreeLookOwner*>(object)->runtime.reset(); //4341781B
}
void SKEET_FREE_OWNER_MEMBER forward_reset_entry(void* object) {
    engine::invoke<void>(object, 5); //433CF2F5: current primary slot5, not a reset copy
}
void initialize_entry(void* context) {
    auto& source = *static_cast<FreeLookInitialization*>(context);
    create_free_look_owner(source.lifecycle, source.updates, source.bindings);
}
#undef SKEET_FREE_OWNER_MEMBER
}

std::size_t free_look_owner_size() noexcept {
    //The production x86 wrapper fits the exact native0x38 allocation. Pointer
    //width changes the semantic wrapper's storage requirement on other hosts.
    return sizeof(FreeLookOwner) > native_free_look_owner_allocation_size
               ? sizeof(FreeLookOwner)
               : native_free_look_owner_allocation_size;
}
FreeLookOwner& construct_free_look_owner(void* storage, engine::OwnerRegistryBinding lifecycle,
                                         engine::OwnerRegistryBinding updates,
                                         FreeLookOwnerBindings bindings) {
    return *::new (storage) FreeLookOwner(lifecycle, updates, bindings);
}
FreeLookOwner* create_free_look_owner(engine::OwnerRegistryBinding lifecycle,
                                      engine::OwnerRegistryBinding updates,
                                      FreeLookOwnerBindings bindings) {
    const auto memory = lifecycle.memory;
    const auto allocation_size = free_look_owner_size() + 0x20u; //43387448; x86=0x58
    auto* raw = memory.allocate(memory.process_heap, 8, allocation_size);
    if (!raw) {
        bindings.active = nullptr; //43484A67 XOR EBX,EBX, then recorded43484A69 store
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15u) & ~std::uintptr_t(15u);
    const auto displacement = static_cast<std::uint32_t>(header - address);
    const auto total = static_cast<std::uint32_t>(allocation_size);
    std::memcpy(reinterpret_cast<void*>(header + 8u), &displacement, sizeof displacement);
    std::memcpy(reinterpret_cast<void*>(header + 12u), &total, sizeof total);
    //Native state occupied this same zeroed heap allocation. The source's
    //command and capture consumers borrow one shared state projection.
    std::memset(&bindings.state, 0, sizeof bindings.state);
    auto& owner = construct_free_look_owner(reinterpret_cast<void*>(header + 16u), lifecycle,
                                            updates, bindings);
    bindings.active = &owner; //mem_int3.h:516, live EBX is this constructed owner
    return &owner;
}
void destroy_free_look_owner(FreeLookOwner* owner, FreeLookOwner*& active,
                             engine::JobMemory memory) {
    active = nullptr; //43406BFF is before433837D0's null-object guard
    if (!owner)
        return;
    owner->~FreeLookOwner();
    auto* bytes = reinterpret_cast<std::byte*>(owner);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
FreeLookOwnerState& free_look_owner_state(FreeLookOwner& owner) noexcept {
    return *owner.canonical;
}
FreeLookViewRuntime& free_look_view_runtime(FreeLookOwner& owner) noexcept {
    return owner.runtime;
}
network::UpdateObserver& free_look_view_endpoint(FreeLookOwner& owner) noexcept {
    return owner.observer.endpoint;
}
void* free_look_lifecycle_object(FreeLookOwner& owner) noexcept {
    return &owner;
}
void bind_free_look_initializer(client::NativeStartupSources& sources,
                                FreeLookInitialization& source) {
    sources.bind_initializer({0x43484A2Bu, &source, initialize_entry});
}

} //namespace skeet::effects
