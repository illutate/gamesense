#include "skeet/misc/zeus_owner.hpp"
#include "skeet/misc/command_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define ZEUS_MEMBER __thiscall
#else
#define ZEUS_MEMBER
#endif
using OwnerEntry = void(ZEUS_MEMBER*)(void*);
void ZEUS_MEMBER destroy_zeus(void*);
void ZEUS_MEMBER initialize_zeus(void*);
void ZEUS_MEMBER unchanged_zeus(void*) {} //433CCE8E RET
// Native4346FEE0. Slot1 is initialization, not an eager constructor action.
const std::array<OwnerEntry, 9> lifecycle_table{destroy_zeus,   initialize_zeus, unchanged_zeus,
                                                unchanged_zeus, unchanged_zeus,  unchanged_zeus,
                                                unchanged_zeus, unchanged_zeus,  unchanged_zeus};
}

class ZeusLifecycleOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    ConfigLink& descriptors;
    ZeusState& state;
    CommandOwner command;
    client::NativeCommandPhaseReceiver command_receiver{};

    ZeusLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                       client::CommandPhaseRegistry& commands, ConfigRegistries& registries,
                       ZeusConfig& settings, ZeusState& shared_state, const ZeusBindings& bindings)
        : memory(lifecycle.memory), descriptors(registries.descriptors), state(shared_state),
          command(ZeusCommandOwner{shared_state, settings, bindings}) {
        lifecycle.append(this); //43485D53 ->433676A0, primary still null
        commands.append(command_receiver, &command); //43485D5B ->43345EBC
        vtable = lifecycle_table.data(); //43485D63, table4346FEE0
        commands.publish(command_receiver, client::CommandOwnerId::zeus); //43485D6B
        initialize_zeus_config(registries, settings); //2E314CE2/size1/type1; value untouched
        state.rage_enabled = nullptr; //43485D86, after descriptor registration
    }
};

namespace {
void ZEUS_MEMBER initialize_zeus(void* object) { //434824A7
    auto& owner = *static_cast<ZeusLifecycleOwner*>(object);
    bind_zeus_configuration(owner.state, owner.descriptors);
}
void ZEUS_MEMBER destroy_zeus(void* object) { //433B1A7A ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<ZeusLifecycleOwner*>(object);
    const auto memory = owner->memory;
    owner->~ZeusLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
    // Native free does not remove borrowed descriptor/command/primary links.
}
}

std::size_t zeus_owner_size() noexcept {
    return sizeof(ZeusLifecycleOwner);
}
ZeusLifecycleOwner& construct_zeus(void* storage, engine::OwnerRegistryBinding lifecycle,
                                   client::CommandPhaseRegistry& commands,
                                   ConfigRegistries& registries, ZeusConfig& settings,
                                   ZeusState& state, const ZeusBindings& bindings) {
    return *::new (storage)
        ZeusLifecycleOwner(lifecycle, commands, registries, settings, state, bindings);
}
#undef ZEUS_MEMBER
} //namespace skeet::misc
