#include "skeet/client/history_runtime_factories.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
using Bindings = HistoryRuntimeFactoryBindings;

bool same_memory(engine::JobMemory first, engine::JobMemory second) noexcept {
    return first.allocate == second.allocate && first.free == second.free &&
           first.process_heap == second.process_heap;
}
void require_bindings(const Bindings& b) {
    const auto& image = engine::require_native_data_image(b.data);
    const auto& registries = b.history.registries;
    if (!b.lifecycle.memory.allocate || !b.lifecycle.memory.free ||
        !b.lifecycle.memory.process_heap)
        throw std::invalid_argument("History factories require their canonical process heap");
    struct Alias {
        const engine::OwnerRegistryBinding* binding;
        std::uint32_t address;
    };
    const std::array aliases{
        Alias{&b.lifecycle, 0x43475b78u}, Alias{&registries.lifecycle, 0x43475b78u},
        Alias{&registries.packets, 0x4347584cu}, Alias{&registries.frame_update, 0x43475ad8u}};
    for (const auto& alias : aliases)
        if (&alias.binding->registry != &image.native_cell<engine::OwnerRegistry>(alias.address) ||
            !same_memory(alias.binding->memory, b.lifecycle.memory))
            throw std::invalid_argument(
                "History factories must share their native registry cells and heap");
    if (&b.entities != &registries.entities || &b.entities.image() != &image ||
        &b.entities.native_storage() != &image.native_cell<engine::OwnerRegistry>(0x43475ba8u))
        throw std::invalid_argument(
            "History factories must share the canonical entity observer registry");
    if (&registries.configuration.descriptors != &image.native_cell<ConfigLink>(0x434798bcu) ||
        &registries.configuration.hotkeys != &image.native_cell<HotkeyLink>(0x434798c4u))
        throw std::invalid_argument(
            "History factories require the canonical configuration sentinels");
    if (&b.history_active != &image.native_cell<lagcomp::HistoryOwnerRuntime*>(0x43475ac0u) ||
        &b.history.local_player != &image.native_cell<void*>(0x43475808u) ||
        &b.prediction.engine_client != &image.native_cell<void*>(0x4346a514u) ||
        &b.prediction.entity_list != &image.native_cell<void*>(0x4346a890u) ||
        &b.prediction.pending_events != &image.native_cell<void*>(0x43475a50u) ||
        &b.prediction.base_client != &image.native_cell<void*>(0x434683e0u) ||
        &b.prediction.replacement_property != &image.native_cell<std::uint32_t*>(0x43469060u))
        throw std::invalid_argument("History factories require their canonical live global cells");
}

void initialize_prediction(void* context) {
    (void)create_prediction_entity_update_owner(*static_cast<Bindings*>(context));
}
void initialize_history(void* context) {
    (void)create_lagcomp_history_owner(*static_cast<Bindings*>(context));
}
}

void* create_prediction_entity_update_owner(HistoryRuntimeFactoryBindings& bindings) {
    require_bindings(bindings);
    auto* const storage = engine::allocate_owner_storage(
        bindings.lifecycle.memory,
        std::max(std::size_t{0x18}, prediction::engine_entity_update_owner_size()));
    if (!storage) {
        //The native null branch enters unresolved INT1/junk at4347E369.
        //Source failure policy is explicit; no null continuation is invented.
        throw std::bad_alloc();
    }
    (void)prediction::construct_engine_entity_update_owner(storage, bindings.lifecycle,
                                                           bindings.entities, bindings.prediction);
    return storage;
}

lagcomp::HistoryOwnerRuntime*
create_lagcomp_history_owner(HistoryRuntimeFactoryBindings& bindings) {
    require_bindings(bindings);
    auto* const storage = engine::allocate_owner_storage(
        bindings.lifecycle.memory, std::max(std::size_t{0x60}, lagcomp::history_owner_size()));
    lagcomp::HistoryOwnerRuntime* owner = nullptr;
    if (storage) {
        //HEAP_ZERO_MEMORY covered this descriptor inside the native owner.
        //Its source projection is borrowed, and is cleared only on success.
        bindings.history.field_ed048f3d = BooleanConfig{};
        owner =
            &lagcomp::construct_history_owner(storage, bindings.history_active, bindings.history);
    }
    //mem_int3.h records43484F7D MOV[43475AC0],EAX. The success constructor
    //returns before this store; the known null branch publishes the same cell.
    bindings.history_active = owner;
    return owner;
}

void bind_history_runtime_factories(NativeStartupSources& sources,
                                    HistoryRuntimeFactoryBindings& bindings) {
    if (&sources.data() != bindings.data.get())
        throw std::invalid_argument("History factories must share the startup native data owner");
    require_bindings(bindings);
    sources.bind_initializer({prediction_entity_update_factory, &bindings, initialize_prediction});
    sources.bind_initializer({lagcomp_history_factory, &bindings, initialize_history});
}
} //namespace skeet::client
