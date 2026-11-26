#include "skeet/prediction/entity_update.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <new>

namespace skeet::prediction {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void put(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
#if defined(_M_IX86) || defined(__i386__)
using LifecycleEntry = void(__thiscall*)(void*);
#define ENTITY_UPDATE_MEMBER __thiscall
#else
using LifecycleEntry = void (*)(void*);
#define ENTITY_UPDATE_MEMBER
#endif
void ENTITY_UPDATE_MEMBER destroy_owner(void*);
void ENTITY_UPDATE_MEMBER unchanged_owner(void*) {} //433CCE8E
const std::array<LifecycleEntry, 9> lifecycle{
    destroy_owner,   unchanged_owner, unchanged_owner, unchanged_owner, unchanged_owner,
    unchanged_owner, unchanged_owner, unchanged_owner, unchanged_owner}; //4346F358
struct EntityUpdateInstance {
    const LifecycleEntry* vtable = nullptr;
    client::NativeEntityObserverReceiver entity_receiver;
    EngineEntityUpdateOwner observer;
    engine::JobMemory memory;
    EntityUpdateInstance(engine::OwnerRegistryBinding primary,
                         client::EntityObserverRegistry& entities,
                         const EngineEntityUpdateBindings& bindings)
        : observer(bindings), memory(primary.memory) {
        primary.append(this);
        register_engine_entity_update_owner(entities, entity_receiver, observer); //434849E2
        vtable = lifecycle.data(); //434849E7
        entities.publish_prediction_update(entity_receiver); //434849F2, table43472388
        //The five masked constructor bytes are proven A1 14 A5 46 43.
        observer.source_interface = bindings.engine_client;
        observer.first_filter_identity = std::uint32_t(reinterpret_cast<std::uintptr_t>(
            engine::find_client_class(bindings.base_client, 0xD929B4F3u)));
        observer.second_filter_identity = std::uint32_t(reinterpret_cast<std::uintptr_t>(
            engine::find_client_class(bindings.base_client, 0x847CC977u)));
    }
};
void ENTITY_UPDATE_MEMBER destroy_owner(void* object) { //433B1A7A→433837D0
    if (!object)
        return;
    auto* owner = static_cast<EntityUpdateInstance*>(object);
    const auto memory = owner->memory;
    owner->~EntityUpdateInstance();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef ENTITY_UPDATE_MEMBER
}
void register_engine_entity_update_owner(client::EntityObserverRegistry& registry,
                                         client::NativeEntityObserverReceiver& receiver,
                                         EngineEntityUpdateOwner& owner) {
    registry.append_prediction_update(receiver, owner);
}
void update_prediction_entities(EngineEntityUpdateOwner& owner) {
    const auto& b = owner.bindings;
    if (engine::invoke<bool>(b.engine_client, 82))
        return;
    if (engine::invoke<bool>(b.engine_client, 93))
        return;
    auto* node = field<void*>(b.pending_events, 0xC);
    while (node) {
        auto* next = field<void*>(node, 0x38);
        const auto identity = field<std::uint32_t>(node, 0xC);
        if (identity == owner.first_filter_identity || identity == owner.second_filter_identity)
            put(node, 4, std::uint32_t(0));
        node = next;
    }
    //Native does not clear this scratch. Each admitted live player supplies
    //its saved word; the same engine entity-validity contract applies at restore.
    std::array<std::uint32_t, 64> saved;
    std::uint64_t touched_mask = 0;
    if (b.replacement_property) {
        for (std::int32_t index = 1; index <= 64; ++index) {
            auto* player = engine::invoke<void*>(b.entity_list, 3, index);
            if (player && engine::invoke<bool>(player, 158)) {
                touched_mask |= (1ULL << (index - 1));
                saved[std::uint32_t(index) & 63u] = field<std::uint32_t>(player, b.entity_property);
                put(player, b.entity_property, *b.replacement_property);
            }
        }
    }
    engine::invoke<void>(owner.source_interface, 59);
    //Read the optional pointer again after the actual engine method. Unlike
    //the CreateMove override, this native loop restores saved zero words too.
    if (b.replacement_property && touched_mask) {
        for (std::uint64_t mask = touched_mask; mask; mask &= mask - 1) {
            const auto index = std::int32_t(std::countr_zero(mask)) + 1;
            auto* player = engine::invoke<void*>(b.entity_list, 3, index);
            if (player && engine::invoke<bool>(player, 158))
                put(player, b.entity_property, saved[std::uint32_t(index) & 63u]);
        }
    }
}
std::size_t engine_entity_update_owner_size() noexcept {
    return sizeof(EntityUpdateInstance);
}
EngineEntityUpdateOwner&
construct_engine_entity_update_owner(void* storage, engine::OwnerRegistryBinding primary,
                                     client::EntityObserverRegistry& entities,
                                     const EngineEntityUpdateBindings& bindings) {
    return (::new (storage) EntityUpdateInstance(primary, entities, bindings))->observer;
}
} //namespace skeet::prediction
