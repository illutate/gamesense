#include "skeet/engine/map_game_system_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/esp/map_entity_parser.hpp"
#include "game_system_detail.hpp"
#include <bit>
#include <cstring>
#include <new>

namespace skeet::engine {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define MAP_SYSTEM_MEMBER __thiscall
#else
#define MAP_SYSTEM_MEMBER
#endif
void MAP_SYSTEM_MEMBER empty(void*) {} //433CCE8E, RET0
bool MAP_SYSTEM_MEMBER true_entry(void*) {
    return true;
} //433422C6
const char* MAP_SYSTEM_MEMBER name(void*) {
    static const char text[1] = {0};
    return text;
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
void grow(MapGameSystemVector& vector, std::int32_t amount, void* allocator) { //4337CF0C
    if (vector.grow_size < 0)
        return;
    const auto requested = add(vector.capacity, amount);
    std::int32_t capacity;
    if (vector.grow_size) {
        const auto preceding = subtract(requested, 1);
        capacity = add(subtract(preceding, preceding % vector.grow_size), vector.grow_size);
    } else {
        capacity = vector.capacity ? vector.capacity : 8;
        while (capacity < requested)
            capacity = add(capacity, capacity);
    }
    if (capacity < requested) {
        if (!capacity && requested <= -1)
            capacity = -1;
        else
            do {
                const auto sum = add(capacity, requested);
                capacity = add(sum, sum < 0 ? 1 : 0) >> 1;
            } while (capacity < requested);
    }
    vector.capacity = capacity;
    const auto bytes = std::uint32_t(capacity) * std::uint32_t(sizeof(MapGameSystemEndpoint*));
    vector.data = vector.data ? invoke<MapGameSystemEndpoint**>(allocator, 3, vector.data, bytes)
                              : invoke<MapGameSystemEndpoint**>(allocator, 1, bytes);
}
void append(MapGameSystemVector& vector, MapGameSystemEndpoint* const* value,
            void* allocator) { //433F4786
    const auto insertion = vector.size;
    auto count = vector.size;
    if (add(insertion, 1) > vector.capacity) {
        grow(vector, add(subtract(insertion, vector.capacity), 1), allocator);
        count = vector.size;
    }
    const auto data = vector.data;
    vector.size = add(count, 1);
    vector.elements = data;
    const auto moved = subtract(count, insertion);
    if (moved > 0)
        std::memmove(data + insertion + 1, data + insertion,
                     std::size_t(moved) * sizeof(MapGameSystemEndpoint*));
    vector.data[insertion] = *value; //native reloads data after the external memmove
}
void remove(MapGameSystemVector& vector, MapGameSystemEndpoint* const* value) { //433B9059
    auto count = vector.size;
    if (count <= 0)
        return;
    std::int32_t index = 0;
    while (vector.data[index] != *value)
        if (++index >= count)
            return;
    const auto moved = subtract(subtract(count, index), 1);
    if (moved > 0) {
        std::memmove(vector.data + index, vector.data + index + 1,
                     std::size_t(moved) * sizeof(MapGameSystemEndpoint*));
        count = vector.size;
    }
    vector.size = subtract(count, 1);
}
MapGameSystemVector* list_of(std::int32_t* size) {
    return reinterpret_cast<MapGameSystemVector*>(reinterpret_cast<std::byte*>(size) -
                                                  offsetof(MapGameSystemVector, size));
}
void factory_entry(void* opaque) {
    static_cast<MapGameSystemRuntime*>(opaque)->create();
}
} //namespace

MapGameSystemRuntime::MapGameSystemRuntime(OwnerRegistryBinding owners,
                                           MapGameSystemBindings bindings)
    : primary_{{destroy_entry, initialize_entry, append_entry, empty, remove_entry, empty, empty,
                empty, empty},
               this},
      endpoint_{this,
                {{name, true_entry, empty, empty, empty, map_entity_text_entry, empty, empty, empty,
                  empty, empty, true_entry, destroy_endpoint_entry},
                 empty,
                 frame_update_entry,
                 empty}},
      destroyed_{this,
                 {reinterpret_cast<GameSystemEntry<const char*>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<bool>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<void>>(owners.memory.purecall),
                  reinterpret_cast<GameSystemEntry<bool>>(owners.memory.purecall),
                  destroy_endpoint_entry}},
      owners_(owners), bindings_(bindings) {}

MapGameSystemRuntime& MapGameSystemRuntime::runtime(MapGameSystemOwner& owner) {
    static_assert(offsetof(PrimaryTable, entries) == 0);
    return *reinterpret_cast<const PrimaryTable*>(owner.vtable)->runtime;
}
MapGameSystemRuntime& MapGameSystemRuntime::runtime(MapGameSystemEndpoint& endpoint) {
    static_assert(offsetof(EndpointTable, entries) == sizeof(void*));
    static_assert(offsetof(DestroyedEndpointTable, entries) == sizeof(void*));
    MapGameSystemRuntime* context;
    std::memcpy(&context, reinterpret_cast<const std::byte*>(endpoint.vtable) - sizeof(context),
                sizeof(context));
    return *context;
}
MapGameSystemOwner* MapGameSystemRuntime::create() {
    auto* storage = game_system_detail::allocate(owners_.memory, sizeof(MapGameSystemOwner));
    if (!storage)
        return nullptr;
    auto* owner = ::new (storage) MapGameSystemOwner;
    owner->vtable = nullptr;
    owners_.append(owner);
    owner->vtable = primary_.entries.data();
    owner->endpoint.vtable = &endpoint_.entries.base;
    return owner;
}
void MapGameSystemRuntime::initialize(MapGameSystemOwner& owner) {
    owner.initialized = &owner;
    auto* first = bindings_.first_size;
    if (!first || !bindings_.second_size)
        return;
    owner.first = list_of(first);
    if (*first)
        owner.second = list_of(bindings_.second_size);
}
void MapGameSystemRuntime::append(MapGameSystemOwner& owner) {
    if (owner.first && owner.first->size) {
        auto* endpoint = &owner.endpoint;
        engine::append(*owner.first, &endpoint, bindings_.allocator);
    }
    if (owner.second && owner.second->size) {
        auto* endpoint = &owner.endpoint;
        engine::append(*owner.second, &endpoint, bindings_.allocator);
    }
}
void MapGameSystemRuntime::remove(MapGameSystemOwner& owner) {
    if (owner.first) {
        auto* endpoint = &owner.endpoint;
        engine::remove(*owner.first, &endpoint);
    }
    if (owner.second) {
        auto* endpoint = &owner.endpoint;
        engine::remove(*owner.second, &endpoint);
    }
}
void MapGameSystemRuntime::destroy(MapGameSystemOwner* owner) {
    if (!owner)
        return;
    owner->endpoint.vtable = &destroyed_.entries;
    game_system_detail::release(owner, owners_.memory);
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::destroy_entry(void* object) {
    if (object)
        runtime(*static_cast<MapGameSystemOwner*>(object))
            .destroy(static_cast<MapGameSystemOwner*>(object));
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::initialize_entry(void* object) {
    auto& owner = *static_cast<MapGameSystemOwner*>(object);
    runtime(owner).initialize(owner);
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::append_entry(void* object) {
    auto& owner = *static_cast<MapGameSystemOwner*>(object);
    runtime(owner).append(owner);
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::remove_entry(void* object) {
    auto& owner = *static_cast<MapGameSystemOwner*>(object);
    runtime(owner).remove(owner);
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::map_entity_text_entry(void* object) {
    auto& state = runtime(*static_cast<MapGameSystemEndpoint*>(object));
    esp::dispatch_map_entity_text(state.bindings_.map_entity_parsers, state.bindings_.engine);
}
void MAP_SYSTEM_MEMBER MapGameSystemRuntime::frame_update_entry(void* object, float seconds) {
    auto& state = runtime(*static_cast<MapGameSystemEndpoint*>(object));
    esp::dispatch_map_scalar(state.bindings_.map_entity_parsers, seconds);
}
void* MAP_SYSTEM_MEMBER MapGameSystemRuntime::destroy_endpoint_entry(void* object,
                                                                     std::uint8_t flags) {
    auto& endpoint = *static_cast<MapGameSystemEndpoint*>(object);
    auto& state = runtime(endpoint);
    endpoint.vtable = &state.destroyed_.entries;
    //433B369E passes this exact endpoint to433837D0, without an owner adjustment.
    if (flags & 1)
        game_system_detail::release(object, state.owners_.memory);
    return object;
}
void bind_map_game_system_factory(client::NativeStartupSources& sources,
                                  MapGameSystemRuntime& runtime) {
    sources.bind_initializer({0x4348E6AAu, &runtime, factory_entry});
}
#undef MAP_SYSTEM_MEMBER
} //namespace skeet::engine
