#include "skeet/misc/reveal_overwatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/network/update_observers.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define REVEAL_MEMBER __thiscall
#define REVEAL_FAST __fastcall
#else
#define REVEAL_MEMBER
#define REVEAL_FAST
#endif
using OwnerEntry = void(REVEAL_MEMBER*)(void*);
struct RevealOverwatchOwner;
struct RevealUpdateEndpoint : network::UpdateObserver {
    RevealOverwatchOwner* owner;
};

void REVEAL_MEMBER destroy_owner(void*);
void REVEAL_MEMBER reset_owner(void*);
void REVEAL_MEMBER forward_reset_owner(void*);
void REVEAL_MEMBER unchanged(void*) {} //433CCE8E, ret0
void REVEAL_MEMBER begin_update(void*);
void REVEAL_MEMBER end_update(void*);
void REVEAL_FAST unchanged_player_view(void*, void*, std::uintptr_t, std::uintptr_t,
                                       std::uintptr_t) {} //4337D653, ret12
void REVEAL_FAST unchanged_view(void*, float*, float*, float*) {} //4334A269, ret8
void REVEAL_FAST unchanged_post_view(void*, void*) {} //433CCE8E, ret0

const std::array<OwnerEntry, 9> owner_table{destroy_owner, unchanged,           unchanged,
                                            unchanged,     unchanged,           reset_owner,
                                            unchanged,     forward_reset_owner, unchanged};
const network::UpdateObserverVtable update_table{
    {unchanged, end_update, unchanged, unchanged, begin_update},
    unchanged_player_view,
    unchanged_view,
    unchanged_post_view};

struct RevealOverwatchOwner {
    const OwnerEntry* vtable = nullptr;
    std::uintptr_t reserved = 0;
    RevealUpdateEndpoint update{{nullptr}, this};
    RevealOverwatchRuntime runtime;
    engine::JobMemory memory;

    RevealOverwatchOwner(engine::OwnerRegistryBinding owners, engine::OwnerRegistryBinding updates,
                         ConfigRegistries& registries, RevealOverwatchConfig& config,
                         void* const& engine_object)
        : runtime(engine_object), memory(owners.memory) {
        owners.append(this); //433676A0, before either vtable is installed
        network::register_update_observer(updates, update); //433B8C1C, owner+8
        vtable = owner_table.data();
        update.vtable = &update_table;
        initialize_reveal_overwatch_config(registries, config);
        runtime.enabled = &config.enabled.value;
        runtime.pending = 0;
    }
};
void REVEAL_MEMBER destroy_owner(void* object) { //433B1A7A ->433837D0
    auto* owner = static_cast<RevealOverwatchOwner*>(object);
    const auto memory = owner->memory;
    owner->~RevealOverwatchOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void REVEAL_MEMBER reset_owner(void* object) {
    static_cast<RevealOverwatchOwner*>(object)->runtime.reset();
}
void REVEAL_MEMBER forward_reset_owner(void* object) { //433CF2F5
    engine::invoke<void>(object, 5);
}
void REVEAL_MEMBER begin_update(void* object) {
    static_cast<RevealUpdateEndpoint*>(object)->owner->runtime.begin_network_update();
}
void REVEAL_MEMBER end_update(void* object) {
    static_cast<RevealUpdateEndpoint*>(object)->owner->runtime.end_network_update();
}

template <class Value> Value field(const void* object, std::ptrdiff_t offset) {
    Value value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
void set_anonymous(void* parameters, std::uint8_t value) {
    std::memcpy(static_cast<std::byte*>(parameters) + 0x10, &value, sizeof value);
}
}

void RevealOverwatchRuntime::begin_network_update() {
    if (!*enabled)
        return;
    auto* parameters = engine::invoke<void*>(engine, 218);
    if (!parameters)
        return;
    const auto anonymous = field<std::uint8_t>(parameters, 0x10);
    if (!anonymous || field<std::int32_t>(parameters, 0x0C) == 0)
        return;
    pending = anonymous;
    set_anonymous(parameters, 0);
}
void RevealOverwatchRuntime::end_network_update() {
    if (!pending)
        return;
    auto* parameters = engine::invoke<void*>(engine, 218);
    if (!parameters)
        return;
    // Native restores literal1, even if the captured nonzero byte was different.
    set_anonymous(parameters, 1);
    pending = 0;
}
void RevealOverwatchRuntime::reset() noexcept {
    pending = 0;
}

std::size_t reveal_overwatch_owner_size() noexcept {
    return sizeof(RevealOverwatchOwner);
}
RevealOverwatchRuntime&
construct_reveal_overwatch(void* storage, engine::OwnerRegistryBinding owners,
                           engine::OwnerRegistryBinding updates, ConfigRegistries& registries,
                           RevealOverwatchConfig& config, void* const& engine_object) {
    auto* owner =
        ::new (storage) RevealOverwatchOwner(owners, updates, registries, config, engine_object);
    return owner->runtime;
}

#undef REVEAL_MEMBER
#undef REVEAL_FAST
} // namespace skeet::misc
