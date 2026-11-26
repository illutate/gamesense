#include "skeet/client/shared_esp_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/misc/network_console_handlers.hpp"
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SHARED_ESP_MEMBER __thiscall
#define SHARED_ESP_FAST __fastcall
#else
#define SHARED_ESP_MEMBER
#define SHARED_ESP_FAST
#endif
using PrimaryEntry = void(SHARED_ESP_MEMBER*)(void*);
struct Instance;
void SHARED_ESP_MEMBER destroy_entry(void*);
void SHARED_ESP_MEMBER forward_reset_entry(void*);
void SHARED_ESP_MEMBER reset_entry(void*);
void SHARED_ESP_MEMBER unchanged_entry(void*) {} //433CCE8E
void SHARED_ESP_FAST message_entry(void*, misc::NetworkMessageCall*);
const std::array<PrimaryEntry, 9> primary_table{
    destroy_entry,       unchanged_entry, unchanged_entry, unchanged_entry, unchanged_entry,
    forward_reset_entry, unchanged_entry, reset_entry,     unchanged_entry}; //43470354
const std::array<misc::NetworkMessageEntry, 1> message_table{message_entry}; //43472B40

template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
const void* decoded_pointer(std::uint32_t value, std::uint32_t mask) noexcept {
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(value ^ mask));
}
engine::MemberFunction<void> call_target(const std::byte* call) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(call) + 5 +
                         static_cast<std::intptr_t>(read<std::int32_t>(call, 1));
    return reinterpret_cast<engine::MemberFunction<void>>(address);
}
void* allocate(engine::JobMemory memory) {
    constexpr std::uint32_t size = 0x2b8, total = size + 0x20;
    auto* const raw = memory.allocate(memory.process_heap, 8, total);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &total, 4);
    return reinterpret_cast<void*>(header + 16);
}

void unchanged_event(void*, events::GameEventReader&, events::EventPlayerIndices&) {} //433C95B7
void reset_event(void*, events::GameEventReader&, events::EventPlayerIndices&);

struct Instance {
    const PrimaryEntry* vtable = nullptr;
    NativeCommandPhaseReceiver command_receiver;
    NativeEntityObserverReceiver entity_receiver;
    engine::JobMemory memory;
    SharedEspOwnerBindings bindings;
    misc::NetworkMessageCallback message{nullptr};
    std::uint32_t auxiliary_identity = 0; //unregistered native full+14
    CommandEmptyTickTimer empty_tick;
    const void* field_b0 = nullptr;
    const void* field_b4 = nullptr;

    Instance(const SharedEspOwnerRegistries& registries, ConfigRegistries& configuration,
             SharedEspOwnerBindings context)
        : memory(registries.primary.memory), bindings(context), empty_tick{context.state.timer_84} {
        registries.primary.append(this); //43485DDA
        registries.commands.append(command_receiver, &bindings.command); //5DE2
        registries.messages.append(&message); //5DEA, before table publication
        auxiliary_identity = 0x4346f3b4u; //43485DF2
        registries.entities.append_shared_esp(entity_receiver, bindings.command); //5DF9
        registries.empty_ticks.append(empty_tick); //43485E01
        registries.commands.publish(command_receiver, CommandOwnerId::owner_4346fe30); //43485E08
        message.vtable = message_table.data(); //43485E0F, network43472B40
        registries.entities.publish_shared_esp(entity_receiver); //43485E18, BA8 table4346F2D0
        registries.empty_ticks.publish(empty_tick); //43485E1F, after BA8 table43485E18
        vtable = primary_table.data(); //43485E33, primary43470354
        auxiliary_identity = 0x4346f3b4u; //43485E39, same unregistered identity
        auto& state = bindings.state;
        state.mixing_key = 0xc8eac742u;
        initialize_hotkey_config(configuration, bindings.settings.activation, 0x7174d684u, 27,
                                 HotkeyMode::always); //43485E47
        initialize_boolean_config(configuration.descriptors, bindings.settings.other_team,
                                  0x6834d5dau); //43485E60
        state.marker = 0x2424;
        //Full+78 points to command.bindings.packet_cipher; +7C/+80 identify
        //the already recovered legacy/RC4 source dispatch, without callbacks.
        const auto& globals = bindings.globals;
        state.engine_client = globals.engine_client; //mem_int3.h:564
        state.send_gate_a = decoded_pointer(globals.encoded_send_gate_a, 0x7b3564bbu);
        state.send_gate_b = decoded_pointer(globals.encoded_send_gate_b, 0x584c3c72u);
        state.game_type = decoded_pointer(globals.encoded_game_type, 0xac9299ccu);
        field_b0 = decoded_pointer(globals.encoded_field_b0, 0xfa2dd713u);
        const auto last = globals.encoded_field_b4;
        state.last_player = 1; //43485EDB
        field_b4 = decoded_pointer(last, 0xac34f836u);

        auto* call = globals.message_constructor_call; //mem_int3.h:570
        if (!call)
            return;
        globals.message_constructor_call = nullptr; //recorded43485EF9 MOV, before target read
        state.construct_message = call_target(call); //43485F07
        if (read<std::uint8_t>(call, 0xb2) != 0xe8)
            return;
        state.destroy_message = call_target(call + 0xb2); //43485F28
        call = globals.receiver_constructor_call; //recorded43485F2E MOV
        if (!call)
            return;
        globals.receiver_constructor_call = nullptr; //recorded43485F38 MOV
        state.construct_receiver = call_target(call); //43485F49
        call += 0x28;
        if (read<std::uint8_t>(call, 0) != 0xe8)
            return;
        state.destroy_receiver = call_target(call); //43485F5D
        registries.events.add({0x3d07d291u, this, unchanged_event}); //43485F74
        registries.events.add({0xf03e64e8u, this, reset_event}); //43485F8B
        registries.events.add({0xdd2bbbc2u, this, unchanged_event}); //43485FA2
    }
};
static_assert(sizeof(Instance) <= 0x2b8);

void require_identity(const SharedEspOwnerBindings& bindings) {
    const auto& command = bindings.command.bindings;
    const auto& incoming = bindings.incoming;
    if (&bindings.active != &command.owner || &bindings.active != &incoming.owner ||
        &bindings.settings != &command.settings ||
        &bindings.settings.enabled.value != &incoming.receive_enabled ||
        &bindings.settings.restrict_updates.value != &incoming.permission_gate ||
        &command.cache != &incoming.cache || &command.players != &incoming.adjustments ||
        &command.entity_list != &incoming.entity_list ||
        &bindings.globals.engine_client != &incoming.engine)
        throw std::invalid_argument(
            "Shared ESP lifecycle requires its canonical command, incoming, configuration and owner slots");
}
void SHARED_ESP_MEMBER destroy_entry(void* storage) {
    auto& instance = *static_cast<Instance*>(storage);
    destroy_shared_esp_owner(storage, instance.bindings.active);
}
void SHARED_ESP_MEMBER forward_reset_entry(void* storage) { //433B06E3
    auto& instance = *static_cast<Instance*>(storage);
    instance.vtable[7](storage);
}
void SHARED_ESP_MEMBER reset_entry(void* storage) {
    reset_shared_esp_owner(storage);
}
void SHARED_ESP_FAST message_entry(void*, misc::NetworkMessageCall* call) { //4336F718
    if (engine::invoke<std::int32_t>(call->message, 7) == 10)
        *call->voice = 1;
}
void reset_event(void* storage, events::GameEventReader&,
                 events::EventPlayerIndices&) { //433B30BB ->433B531F
    auto& instance = *static_cast<Instance*>(storage);
    for (std::uint32_t index = 1; index <= 64; ++index) {
        auto& geometry = instance.bindings.command.bindings.cache.get(index)->geometry;
        geometry.last_live_tick = 0;
        geometry.alpha = 0.0f;
    }
    instance.bindings.state.timer_84 = 2.0f;
}
void initialize_entry(void* context) {
    auto& source = *static_cast<SharedEspInitialization*>(context);
    create_shared_esp_owner(source.registries, source.configuration, source.bindings);
}
} // namespace

std::size_t shared_esp_owner_size() noexcept {
    return 0x2b8;
}
void* construct_shared_esp_owner(void* storage, const SharedEspOwnerRegistries& registries,
                                 ConfigRegistries& configuration, SharedEspOwnerBindings bindings) {
    require_identity(bindings);
    return ::new (storage) Instance(registries, configuration, bindings);
}
void* create_shared_esp_owner(const SharedEspOwnerRegistries& registries,
                              ConfigRegistries& configuration, SharedEspOwnerBindings bindings) {
    require_identity(bindings);
    auto* const storage = allocate(registries.primary.memory);
    if (storage) {
        //43387448's zeroed native allocation covers these borrowed projections.
        //The separate static43467F98/43468598 descriptors are not in this block.
        bindings.state = RejectedPlayerUpdateOwner{};
        bindings.state.mixing_key = 0;
        bindings.state.marker = 0;
        bindings.state.last_player = 0;
        bindings.settings.activation = HotkeyConfig{};
        bindings.settings.other_team = BooleanConfig{};
        construct_shared_esp_owner(storage, registries, configuration, bindings);
        bindings.active = &bindings.state; //4348DA91, mem_int3.h:620
    } else
        bindings.active = nullptr; //raw4348DA8F XOR, same recorded publication
    return storage;
}
void destroy_shared_esp_owner(void* storage, RejectedPlayerUpdateOwner*& active) {
    active = nullptr; //433801D6, before433837D0's null guard
    if (!storage)
        return;
    auto* instance = static_cast<Instance*>(storage);
    const auto memory = instance->memory;
    instance->~Instance();
    auto* const bytes = static_cast<std::byte*>(storage);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void reset_shared_esp_owner(void* storage) { //433C0954
    auto& instance = *static_cast<Instance*>(storage);
    for (std::uint32_t index = 1; index <= 64; ++index)
        instance.bindings.command.bindings.cache.get(index)->geometry.last_live_tick = 0;
}
void bind_shared_esp_initializer(NativeStartupSources& sources, SharedEspInitialization& context) {
    sources.bind_initializer({0x4348da78u, &context, initialize_entry});
}
#undef SHARED_ESP_MEMBER
#undef SHARED_ESP_FAST
} // namespace skeet::client
