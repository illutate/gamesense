#include "skeet/network/packet_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::network {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define PACKET_MEMBER __thiscall
#else
#define PACKET_MEMBER
#endif
using PrimaryEntry = void(PACKET_MEMBER*)(void*);
void PACKET_MEMBER destroy_entry(void*);
void PACKET_MEMBER unchanged(void*) {} //433CCE8E is exactly RET.
void PACKET_MEMBER reset_entry(void*);
void PACKET_MEMBER refresh_entry(void*);
const std::array<PrimaryEntry, 9> primary_table{destroy_entry, unchanged,     unchanged,
                                                unchanged,     unchanged,     reset_entry,
                                                unchanged,     refresh_entry, unchanged}; //4347290C
}

class PacketLifecycleOwner {
public:
    const PrimaryEntry* vtable = nullptr; //434901E1 clears the primary word.
    engine::JobMemory memory;
    PacketCommandOwner& command;
    client::NativeCommandPhaseReceiver command_receiver;

    PacketLifecycleOwner(engine::OwnerRegistryBinding primary,
                         client::CommandPhaseRegistry& commands, ConfigRegistries& registries,
                         anti_aim::FakeLagSettings& settings, PacketCommandOwner& source)
        : memory(primary.memory), command(source) {
        primary.append(this); //433676A0, full+0
        commands.append(command_receiver, &command); //434901EB, full+8
        vtable = primary_table.data(); //434901F2, primary4347290C
        commands.publish(command_receiver, client::CommandOwnerId::fake_lag); //434901FC
        initialize_integer_config(registries.descriptors, settings.amount, 0x2A71D5FCu, 0);
        initialize_integer_config(registries.descriptors, settings.variance, 0xCA9768E6u, 0);
        initialize_integer_config(registries.descriptors, settings.limit, 0xF17FEF11u, 13);
        initialize_packet_command_state(command.state);
    }
};

namespace {
void PACKET_MEMBER destroy_entry(void* storage) {
    destroy_packet_owner(storage);
}
void PACKET_MEMBER reset_entry(void* storage) { //43369ACB
    reset_fake_lag(static_cast<PacketLifecycleOwner*>(storage)->command.state);
}
void PACKET_MEMBER refresh_entry(void* storage) { //433CF2F5
    const auto* owner = static_cast<PacketLifecycleOwner*>(storage);
    owner->vtable[5](storage);
}
}

std::size_t packet_owner_size() noexcept {
    return sizeof(PacketLifecycleOwner);
}
PacketLifecycleOwner& construct_packet_owner(void* storage, engine::OwnerRegistryBinding primary,
                                             client::CommandPhaseRegistry& commands,
                                             ConfigRegistries& registries,
                                             anti_aim::FakeLagSettings& settings,
                                             PacketCommandOwner& command) {
    return *::new (storage) PacketLifecycleOwner(primary, commands, registries, settings, command);
}
void destroy_packet_owner(void* storage) {
    if (!storage)
        return; //433837D0 null guard
    auto* const owner = static_cast<PacketLifecycleOwner*>(storage);
    const auto memory = owner->memory;
    owner->~PacketLifecycleOwner();
    auto* const bytes = static_cast<std::byte*>(storage);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef PACKET_MEMBER
} //namespace skeet::network
