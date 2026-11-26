#include "skeet/misc/clan_tag_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
void process_clan_tag_message(ClanTagRuntime& runtime,
                              const client::ScriptCommandEnvironment& environment,
                              NetworkMessageCall& call) {
    if (!runtime.changed)
        return;
    if (engine::invoke<std::int32_t>(call.message, 7) != 18)
        return;
    if (client::command_roll_blocked(environment))
        return;
    const std::uint8_t value = 0;
    std::memcpy(static_cast<std::byte*>(call.message) + 0x18, &value, sizeof value);
    *call.voice = 1;
}
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define CLAN_MEMBER __thiscall
#define CLAN_FAST __fastcall
#else
#define CLAN_MEMBER
#define CLAN_FAST
#endif
using OwnerEntry = void(CLAN_MEMBER*)(void*);
struct ClanTagInstance;
struct MessageEndpoint : NetworkMessageCallback {
    ClanTagInstance* owner;
};
void CLAN_MEMBER destroy(void*);
void CLAN_MEMBER unchanged(void*) {} //433CCE8E
void CLAN_MEMBER reset(void*);
void CLAN_MEMBER reset_forwarder(void*);
void CLAN_FAST message_receiver(void*, NetworkMessageCall*);
const std::array<OwnerEntry, 9> lifecycle_table{destroy,   unchanged,       unchanged,
                                                unchanged, unchanged,       reset,
                                                unchanged, reset_forwarder, unchanged};
const std::array<NetworkMessageEntry, 1> message_table{message_receiver};

struct ClanTagInstance {
    const OwnerEntry* vtable = nullptr;
    client::NativeEntityObserverReceiver entity_receiver{};
    MessageEndpoint message{{nullptr}, this};
    ClanTagRuntime& runtime;
    const client::ScriptCommandEnvironment& environment;
    engine::JobMemory memory;
    ClanTagInstance(engine::OwnerRegistryBinding owners, client::EntityObserverRegistry& updates,
                    engine::OwnerRegistryBinding messages, ConfigRegistries& registry,
                    ClanTagConfig& settings, ClanTagRuntime& state,
                    const client::ScriptCommandEnvironment& context)
        : runtime(state), environment(context), memory(owners.memory) {
        owners.append(this); //4347E09A
        updates.append_clan_tag(entity_receiver, runtime); //4347E0A2
        messages.append(&message); //4347E0AA
        vtable = lifecycle_table.data(); //4347E0B2, table4346F728
        updates.publish_clan_tag(entity_receiver); //4347E0BA, table4346EEA0
        message.vtable = message_table.data(); //4347E0C2
        initialize_clan_tag_config(registry, settings);
        runtime.enabled = &settings.enabled.value;
        runtime.was_enabled = 0;
        runtime.changed = 0;
        runtime.last_frame = 0;
    }
};
void CLAN_MEMBER destroy(void* object) { //433B1A7A ->433837D0
    auto* owner = static_cast<ClanTagInstance*>(object);
    const auto memory = owner->memory;
    owner->~ClanTagInstance();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void CLAN_MEMBER reset(void* object) {
    static_cast<ClanTagInstance*>(object)->runtime.reset();
}
void CLAN_MEMBER reset_forwarder(void* object) { //433CF2F5 calls current primaryslot5
    static_cast<ClanTagInstance*>(object)->vtable[5](object);
}
void CLAN_FAST message_receiver(void* object, NetworkMessageCall* call) { //4336E573
    auto* endpoint = static_cast<MessageEndpoint*>(static_cast<NetworkMessageCallback*>(object));
    process_clan_tag_message(endpoint->owner->runtime, endpoint->owner->environment, *call);
}
#undef CLAN_MEMBER
#undef CLAN_FAST
}
std::size_t clan_tag_owner_size() noexcept {
    return sizeof(ClanTagInstance);
}
void construct_clan_tag_owner(void* storage, engine::OwnerRegistryBinding owners,
                              client::EntityObserverRegistry& updates,
                              engine::OwnerRegistryBinding messages, ConfigRegistries& registry,
                              ClanTagConfig& settings, ClanTagRuntime& runtime,
                              const client::ScriptCommandEnvironment& context) {
    ::new (storage)
        ClanTagInstance(owners, updates, messages, registry, settings, runtime, context);
}
} //namespace skeet::misc
