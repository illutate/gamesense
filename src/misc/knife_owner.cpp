#include "skeet/misc/knife_owner.hpp"
#include "skeet/misc/command_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define KNIFE_MEMBER __thiscall
#else
#define KNIFE_MEMBER
#endif
using OwnerEntry = void(KNIFE_MEMBER*)(void*);
void KNIFE_MEMBER destroy_knife(void*);
void KNIFE_MEMBER unchanged_knife(void*) {} //433CCE8E RET
const std::array<OwnerEntry, 9> lifecycle_table{destroy_knife,   unchanged_knife, unchanged_knife,
                                                unchanged_knife, unchanged_knife, unchanged_knife,
                                                unchanged_knife, unchanged_knife, unchanged_knife};
}

class KnifeLifecycleOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    CommandOwner command;
    client::NativeCommandPhaseReceiver command_receiver{};

    KnifeLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                        client::CommandPhaseRegistry& commands, ConfigRegistries& registries,
                        KnifeConfig& settings, KnifeCommandBindings bindings,
                        std::uint32_t& scheduler_flags)
        : memory(lifecycle.memory),
          command(KnifeCommandOwner{settings, bindings, scheduler_flags}) {
        lifecycle.append(this); //434802E9 ->433676A0, before vtable assignment
        commands.append(command_receiver, &command); //434802F1 ->43345EBC
        vtable = lifecycle_table.data(); //434802F9, native4346F358
        commands.publish(command_receiver, client::CommandOwnerId::knife); //43480301
        initialize_knife_config(registries, settings); //43480317 /43480326
    }
};

namespace {
void KNIFE_MEMBER destroy_knife(void* object) {
    // Native433B1A7A jumps directly to433837D0. Config and command ranges are
    // released by the common shutdown; this slot does not unlink descriptors.
    if (!object)
        return;
    auto* owner = static_cast<KnifeLifecycleOwner*>(object);
    const auto memory = owner->memory;
    owner->~KnifeLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
std::size_t knife_owner_size() noexcept {
    return sizeof(KnifeLifecycleOwner);
}
KnifeLifecycleOwner& construct_knife(void* storage, engine::OwnerRegistryBinding lifecycle,
                                     client::CommandPhaseRegistry& commands,
                                     ConfigRegistries& registries, KnifeConfig& settings,
                                     KnifeCommandBindings bindings,
                                     std::uint32_t& scheduler_flags) {
    return *::new (storage)
        KnifeLifecycleOwner(lifecycle, commands, registries, settings, bindings, scheduler_flags);
}
#undef KNIFE_MEMBER
} // namespace skeet::misc
