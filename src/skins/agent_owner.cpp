#include "skeet/skins/agent_owner.hpp"
#include "skeet/network/data_update_observers.hpp"
#include <array>
#include <cstring>
#include <new>
#include <optional>

namespace skeet::skins {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define AGENT_MEMBER __thiscall
#else
#define AGENT_MEMBER
#endif
using OwnerEntry = void(AGENT_MEMBER*)(void*);
struct AgentOwner;
void AGENT_MEMBER destroy(void*);
void AGENT_MEMBER unchanged(void*) {} //433CCE8E, all remaining primary slots
const std::array<OwnerEntry, 9> lifecycle_table{destroy,   unchanged, unchanged,
                                                unchanged, unchanged, unchanged,
                                                unchanged, unchanged, unchanged};

struct AgentOwner {
    const OwnerEntry* vtable = nullptr;
    AgentOwnerBindings bindings;
    engine::JobMemory memory;
    std::optional<NativeAgentRuntime> runtime;
    std::array<std::int32_t, 2> constructor_cells_5c_60;
    AgentOwner(engine::OwnerRegistryBinding owners, ConfigRegistries& registry,
               AgentOwnerBindings input)
        : bindings(input), memory(owners.memory) {
        owners.append(this); //4347DAB2, original primary pointer is still zero
        const auto& capture = bindings.receiver.capture_globals();
        const auto& receive = bindings.receiver.receive_bindings();
        const NativeAgentBindings context{receive.engine_client,   bindings.entity_list,
                                          capture.original_model,  capture.original_handle,
                                          bindings.support_models, bindings.offsets};
        // NativeAgentRuntime's mandatory constructor append is deliberately
        // delayed until after the primary owner, preserving 4347DAB7 ordering.
        runtime.emplace(bindings.settings, bindings.catalog, context, bindings.models,
                        bindings.tables, bindings.observers);
        vtable = lifecycle_table.data();
        bindings.observers.publish(*runtime); //4347DACA, after primary4347DAC2
        auto& settings = bindings.settings;
        initialize_boolean_config(registry.descriptors, settings.agent_t, 0x191A5A61);
        initialize_boolean_config(registry.descriptors, settings.agent_ct, 0x6CAF4832);
        initialize_integer_config(registry.descriptors, settings.agent_model_t, 0x3EB3F20B, 0);
        initialize_integer_config(registry.descriptors, settings.agent_model_ct, 0x8191D9BC, 0);
        constructor_cells_5c_60 = {-1, -1};
        // These two owner-local words are not the receive proxy's global
        // capture cells. The original constructor does not reset those cells.
    }
};
void AGENT_MEMBER destroy(void* object) { //433B1A7A ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<AgentOwner*>(object);
    const auto memory = owner->memory;
    owner->~AgentOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef AGENT_MEMBER
}
std::size_t agent_owner_size() noexcept {
    return sizeof(AgentOwner);
}
void construct_agent_owner(void* storage, engine::OwnerRegistryBinding owners,
                           ConfigRegistries& registry, AgentOwnerBindings bindings) {
    ::new (storage) AgentOwner(owners, registry, bindings);
}
void* allocate_agent_owner_storage(engine::JobMemory memory) {
    const auto size = sizeof(AgentOwner);
    auto* allocation = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!allocation)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - base);
    const auto allocation_size = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void bind_agent_owner_receiver(void* object, client::EntityUpdatePatch& patch) noexcept {
    bind_agent_model_receiver(static_cast<AgentOwner*>(object)->bindings.receiver, patch);
}
} //namespace skeet::skins
