#include "skeet/skins/inventory_owner.hpp"
#include <array>
#include <cstring>
#include <new>
#include <optional>

namespace skeet::skins {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define INVENTORY_MEMBER __thiscall
#else
#define INVENTORY_MEMBER
#endif
using OwnerEntry = void(INVENTORY_MEMBER*)(void*);
void INVENTORY_MEMBER destroy_inventory(void*);
void INVENTORY_MEMBER unchanged_inventory(void*) {} //433CCE8E RET
// Native4346F358: no extra initialization/reset/update operation is present.
const std::array<OwnerEntry, 9> lifecycle_table{
    destroy_inventory,   unchanged_inventory, unchanged_inventory,
    unchanged_inventory, unchanged_inventory, unchanged_inventory,
    unchanged_inventory, unchanged_inventory, unchanged_inventory};
}

class InventoryLifecycleOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    InventoryOwnerBindings bindings;
    void* resolver = nullptr;
    client::NativeCommandPhaseReceiver command_receiver{};
    client::NativeEntityObserverReceiver entity_receiver{};
    CommandRuntime command;
    std::optional<NativeInventoryRuntime> inventory;
    events::StatTrakDeath stattrak;
    events::StatTrakDeathListener death_listener;

    InventoryLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                            client::CommandPhaseRegistry& commands,
                            client::EntityObserverRegistry& updates, ConfigRegistries& registry,
                            InventoryOwnerBindings shared)
        : memory(lifecycle.memory), bindings(shared),
          command(shared.menu, shared.refresh, shared.offsets, shared.classification),
          stattrak{shared.weapons}, death_listener{stattrak, shared.death, shared.death_offsets} {
        lifecycle.append(this); //434902E7 /433676A0; primary table still null
        commands.append(command_receiver, &command); //434902EF /43345EBC
        // The runtime retains the owner's later resolver capture cell.
        inventory.emplace(bindings.weapons,
                          NativeInventoryBindings{bindings.engine_client, resolver,
                                                  bindings.counter_rows, bindings.counters,
                                                  bindings.offsets},
                          bindings.random, bindings.refresh);
        updates.append_inventory(entity_receiver, *inventory); //434902F7 /4333BF32
        vtable = lifecycle_table.data(); //434902FC
        commands.publish(command_receiver, client::CommandOwnerId::owner_4346f1f4); //43490305
        updates.publish_inventory(entity_receiver); //4349030E
        resolver = bindings.entity_list; //43490315..31A
        command.previous_weapon = 0; //4349031F, signed word nativeowner+18
        auto& weapons = bindings.weapons;
        weapons.hash = 0xB6218FECu;
        weapons.serialized_size = 0x410;
        weapons.type = ConfigType::integer_array;
        append_config_link(registry.descriptors, weapons); //43490337 /43385AE4
        for (auto& value : weapons.value) { //43490344..35A; reserved+2 is untouched
            value.enabled = value.stattrak = 0;
            value.quality = 100;
            value.seed = value.paintkit = 0;
        }
        death_listener.bind(bindings.events); //43490360..36E /433F9037 /3461B584
    }
    InventoryLifecycleOwner(const InventoryLifecycleOwner&) = delete;
    InventoryLifecycleOwner& operator=(const InventoryLifecycleOwner&) = delete;
};

namespace {
void INVENTORY_MEMBER destroy_inventory(void* object) { //433B1A7A ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<InventoryLifecycleOwner*>(object);
    const auto memory = owner->memory;
    owner->~InventoryLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
    // The original primary destructor does not unlink any borrowed registry.
}
}

std::size_t inventory_owner_size() noexcept {
    return sizeof(InventoryLifecycleOwner);
}
InventoryLifecycleOwner& construct_inventory_owner(void* storage,
                                                   engine::OwnerRegistryBinding lifecycle,
                                                   client::CommandPhaseRegistry& commands,
                                                   client::EntityObserverRegistry& updates,
                                                   ConfigRegistries& registry,
                                                   InventoryOwnerBindings bindings) {
    return *::new (storage)
        InventoryLifecycleOwner(lifecycle, commands, updates, registry, bindings);
}
#undef INVENTORY_MEMBER
} //namespace skeet::skins
