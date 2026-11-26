#include "skeet/misc/automatic_weapons_owner.hpp"
#include "skeet/misc/command_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define AUTOMATIC_MEMBER __thiscall
#else
#define AUTOMATIC_MEMBER
#endif
using OwnerEntry = void(AUTOMATIC_MEMBER*)(void*);
void AUTOMATIC_MEMBER destroy_automatic_weapons(void*);
void AUTOMATIC_MEMBER unchanged_automatic_weapons(void*) {} //actual433CCE8E RET
const std::array<OwnerEntry, 9> lifecycle_table{
    destroy_automatic_weapons,   unchanged_automatic_weapons, unchanged_automatic_weapons,
    unchanged_automatic_weapons, unchanged_automatic_weapons, unchanged_automatic_weapons,
    unchanged_automatic_weapons, unchanged_automatic_weapons, unchanged_automatic_weapons};
}

class AutomaticWeaponsLifecycleOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    CommandOwner command;
    client::NativeCommandPhaseReceiver command_receiver{};

    AutomaticWeaponsLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                                   client::CommandPhaseRegistry& commands,
                                   ConfigRegistries& registries, AutomaticWeaponsConfig& settings,
                                   AutomaticWeaponsState& state,
                                   const AutomaticWeaponsBindings& bindings)
        : memory(lifecycle.memory),
          command(AutomaticWeaponsCommandOwner{state, settings, bindings}) {
        lifecycle.append(this); //4347D999 ->433676A0, before primary table assignment
        commands.append(command_receiver, &command); //4347D9A1 ->43345EBC
        vtable = lifecycle_table.data(); //4347D9A9, table4346F358
        commands.publish(command_receiver, client::CommandOwnerId::automatic_weapons); //4347D9B1
        initialize_automatic_weapons_config(registries, settings);
        state.delay_adjustment = 0;
        state.last_shot_time = 0.0f;
    }
};

namespace {
void AUTOMATIC_MEMBER destroy_automatic_weapons(void* object) {
    // Native433B1A7A jumps directly to433837D0. The configuration and command
    // registries are not edited here; their common shutdown owns their ranges.
    auto* owner = static_cast<AutomaticWeaponsLifecycleOwner*>(object);
    const auto memory = owner->memory;
    owner->~AutomaticWeaponsLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
std::size_t automatic_weapons_owner_size() noexcept {
    return sizeof(AutomaticWeaponsLifecycleOwner);
}
AutomaticWeaponsLifecycleOwner&
construct_automatic_weapons(void* storage, engine::OwnerRegistryBinding lifecycle,
                            client::CommandPhaseRegistry& commands, ConfigRegistries& registries,
                            AutomaticWeaponsConfig& settings, AutomaticWeaponsState& state,
                            const AutomaticWeaponsBindings& bindings) {
    return *::new (storage)
        AutomaticWeaponsLifecycleOwner(lifecycle, commands, registries, settings, state, bindings);
}
#undef AUTOMATIC_MEMBER
} //namespace skeet::misc
