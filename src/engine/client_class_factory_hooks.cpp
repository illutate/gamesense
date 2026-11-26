#include "skeet/engine/client_class_factory_hooks.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/entity_command_runtime_owner.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/client/commands.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/engine/hooks.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/rtti.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace skeet::engine {
namespace {
ClientClassFactoryRuntime* active = nullptr;
ClientClassFactoryRuntime& runtime() {
    if (!active)
        throw std::logic_error("ClientClass creation hooks require their actual source runtime");
    return *active;
}
void* create_player(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::player, index, serial);
}
void* create_ragdoll(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::ragdoll, index, serial);
}
void* create_incendiary(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::incendiary, index, serial);
}
void* create_molotov(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::molotov, index, serial);
}
void* create_viewmodel(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::viewmodel, index, serial);
}
void* create_inferno(std::int32_t index, std::int32_t serial) {
    return runtime().create(ClientClassFactoryKind::inferno, index, serial);
}
constexpr std::uint32_t hash(const char* name) {
    std::uint32_t result = 0x811C9DC5u;
    for (; *name; ++name)
        result = (result ^ static_cast<std::uint8_t>(*name)) * 0x1000193u;
    return result ^ 0x92CA5DF1u;
}
constexpr std::array<ClientClassFactoryDescriptor, 6> recipes{
    {{0x480, "CCSPlayer", hash("CCSPlayer"), 0x4336BD6C, 0x43476AEC, create_player},
     {0x48C, "CCSRagdoll", hash("CCSRagdoll"), 0x433B147B, 0x4347695C, create_ragdoll},
     {0x498, "CIncendiaryGrenade", hash("CIncendiaryGrenade"), 0x4336ACFB, 0x43476B1C,
      create_incendiary},
     {0x4A4, "CMolotovGrenade", hash("CMolotovGrenade"), 0x433D5A0F, 0x43476A9C, create_molotov},
     {0x4B0, "CPredictedViewModel", hash("CPredictedViewModel"), 0x433F20C5, 0x43476ADC,
      create_viewmodel},
     {0x4BC, "CInferno", hash("CInferno"), 0x433F8F74, 0x43476A0C, create_inferno}}};
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class T> void write(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::ptrdiff_t offset(const NativeOffsetGlobals& globals, std::uint32_t pointer, std::uint32_t key,
                      std::uint32_t salt) {
    return DecodedOffset(globals, pointer, key, salt, 0);
}
}
std::span<const ClientClassFactoryDescriptor, 6> client_class_factory_descriptors() noexcept {
    return recipes;
}
ClientClassFactoryRuntime::ClientClassFactoryRuntime(client::ClientRuntimeStorage& storage,
                                                     const client::EngineRuntimeBindings& engine,
                                                     client::PlayerRuntimeOwner& players,
                                                     client::EntityCommandRuntimeOwner& entity,
                                                     HookRuntimeOwner& hooks)
    : ClientClassFactoryRuntime(storage, engine, players, entity, hooks.registry(),
                                hooks.descriptor_storage()) {
    if (&hooks.storage() != &storage)
        throw std::invalid_argument("ClientClass hooks require the canonical hook owner");
}
ClientClassFactoryRuntime::ClientClassFactoryRuntime(client::ClientRuntimeStorage& storage,
                                                     const client::EngineRuntimeBindings& engine,
                                                     client::PlayerRuntimeOwner& players,
                                                     client::EntityCommandRuntimeOwner& entity,
                                                     VtableHookRegistry& registry,
                                                     HookDescriptorStorage& descriptors)
    : storage_(storage), players_(players), entity_(entity), registry_(registry),
      descriptors_(descriptors) {
    if (&engine.variables.data() != &storage.data() || &players.storage() != &storage ||
        &entity.storage() != &storage || &descriptors.image() != &storage.data() ||
        !storage.data().native_cell<void*>(0x4346A514u))
        throw std::invalid_argument(
            "ClientClass hooks require the actual engine/player/prediction/descriptor owners");
}
ClientClassFactoryRuntime::~ClientClassFactoryRuntime() {
    if (active == this)
        active = nullptr;
}
void bind_client_class_factory_runtime(ClientClassFactoryRuntime& value) {
    if (active && active != &value)
        throw std::logic_error("ClientClass creation hooks already have a live source runtime");
    active = &value;
}
void ClientClassFactoryRuntime::install(void* receiver, std::uint32_t descriptor) {
    install_vtable_hook(registry_, receiver, descriptors_.at(descriptor));
}
void* ClientClassFactoryRuntime::create(ClientClassFactoryKind kind, std::int32_t index,
                                        std::int32_t serial) {
    const auto recipe_index = static_cast<std::size_t>(kind);
    const auto original_word =
        storage_.data()
            .native_cell<client::EntityUpdatePatch>(recipes.at(recipe_index).patch_address)
            .after;
    const auto original = reinterpret_cast<ClientClassCreate>(
        static_cast<std::uintptr_t>(std::bit_cast<std::uint32_t>(original_word)));
    auto* result = original(index, serial);
    if (!result)
        return result;
    if (kind == ClientClassFactoryKind::player)
        initialize_player(result, index);
    else if (kind == ClientClassFactoryKind::ragdoll) {
        auto* engine = storage_.data().native_cell<void*>(0x4346A514u);
        if (!invoke<bool>(engine, 82) &&
            !invoke<bool>(storage_.data().native_cell<void*>(0x4346A514u), 93))
            install(result, 0x43475AEC);
    } else {
        auto* unknown = invoke<void*>(result, 0);
        auto* entity = invoke<void*>(unknown, 7);
        constexpr std::array<std::uint32_t, 4> descriptor{0x43475B18, 0x43475A08, 0x43475B34,
                                                          0x43475AC4};
        install(entity, descriptor.at(recipe_index - 2));
    }
    return result;
}
void ClientClassFactoryRuntime::restore_local_movement_flags(void* player) { //433FD037
    auto& data = storage_.data();
    if (data.native_cell<std::int32_t>(0x4347C9ECu) != 2 ||
        !invoke<bool>(data.native_cell<void*>(0x4346A514u), 26))
        return;
    if (*data.native_cell<const std::int32_t*>(0x434758D8u) != -1 &&
        invoke<std::int32_t>(data.native_cell<void*>(0x4346A514u), 205) !=
            data.native_cell<std::int32_t>(0x4346D4B8u))
        return;
    if (invoke<std::int32_t>(data.native_cell<void*>(0x4346A514u), 205) <= 10)
        return;
    const auto command_number = static_cast<std::uint32_t>(invoke<std::int32_t>(
                                    data.native_cell<void*>(0x4346A514u), 205)) -
                                10u;
    const auto* commands = read<const client::Command*>(data.native_cell<void*>(0x4346A894u), 0xF0);
    if (static_cast<std::uint32_t>(commands[command_number % 150u].number) != command_number)
        return;
    const auto* movement = read<const void*>(data.native_cell<void*>(0x4346A7F8u), 8);
    if (!movement)
        return;
    const auto handle = read<std::uint32_t>(movement, 4);
    const auto player_index = handle == UINT32_MAX ? 0x1FFFu : static_cast<std::uint16_t>(handle);
    if (player_index !=
        static_cast<std::uint32_t>(invoke<std::int32_t>(data.native_cell<void*>(0x4346A514u), 12)))
        return;
    const auto flags = read<std::uint32_t>(movement, 0x28);
    if (flags & 0x400000u)
        write(player, offset(storage_.offsets().globals, 0x4346A3E4u, 0x43467AF0u, 0xD1C1093Eu),
              flags);
}
void ClientClassFactoryRuntime::initialize_player(void* networkable,
                                                  std::int32_t index) { //4336BD6C
    auto& data = storage_.data();
    if (invoke<bool>(data.native_cell<void*>(0x4346A514u), 82) ||
        invoke<bool>(data.native_cell<void*>(0x4346A514u), 93))
        return;
    auto* unknown = invoke<void*>(networkable, 0);
    auto* player = invoke<void*>(unknown, 7);
    auto* thinkable = invoke<void*>(unknown, 8);
    auto* owner = players_.cache().get(static_cast<std::uint32_t>(index));
    auto* owner_bytes = reinterpret_cast<std::byte*>(&owner->native);
    if (data.native_cell<std::int32_t>(0x4347C9ECu) == 0) {
        lagcomp::clear_history_records(owner->history, players_.records());
        std::memset(&owner->native, 0, sizeof owner->native);
        owner->history = {};
        owner->publish_history();
        write(owner_bytes, 0xAA, std::uint16_t(0xFFFF));
    }
    const auto& globals = storage_.offsets().globals;
    write(player, offset(globals, 0x43468DC4u, 0x4346895Cu, 0x1E54010Cu), std::uint8_t(0));
    std::array<std::byte, 0x158> info;
    if (invoke<bool>(data.native_cell<void*>(0x4346A514u), 8, index, info.data())) {
        const auto identity =
            data.native_cell<std::uint32_t>(0x4347BE00u + static_cast<std::uint32_t>(index) * 32u);
        if (identity != read<std::uint32_t>(info.data(), 8))
            players_.adjustments()
                .records[static_cast<std::uint32_t>(index)]
                .adjustments.correction_active = 0;
    }
    write(owner_bytes, 0x17C,
          read<std::uint8_t>(player, offset(globals, 0x43468DBCu, 0x434689C8u, 0x8481EA04u)));
    if (data.native_cell<std::int32_t>(0x4347C9ECu) == 0)
        std::memset(owner_bytes + 0x2FC, 0, 0x16C);
    std::memset(owner_bytes + 0x468, 0, 0x16C);
    if (index != invoke<std::int32_t>(data.native_cell<void*>(0x4346A514u), 12)) {
        install(networkable, 0x43475A28);
        install(player, 0x43475830);
        install(thinkable, 0x43475B60);
        if (auto* renderable = invoke<void*>(unknown, 5))
            install(renderable, 0x4347589C);
    } else {
        restore_local_movement_flags(player);
        entity_.initialize_prediction_scratch(true);
        install(networkable, 0x43475B48);
        install(player, 0x43475954);
        install(thinkable, 0x43475878);
        data.native_cell<void*>(0x43475808u) = player;
        if (auto* renderable = invoke<void*>(unknown, 5))
            install(renderable, 0x43475998);
    }
    if (!descriptors_.at(0x43475864).replacement_table)
        return;
    auto& listener_offset = data.native_cell<std::int32_t>(0x43479CE4u);
    if (!listener_offset) {
        auto** table = read<void**>(player, 0);
        const auto* locator = static_cast<const RttiObjectLocator*>(table[-1]);
        for (std::uint32_t i = 0; i < locator->hierarchy->count; ++i) {
            const auto* base = locator->hierarchy->bases[i];
            const auto name_hash = netvar_name_hash(base->type->name);
            if (name_hash == 0x440C879Du || name_hash == 0xC7BCB2A1u) {
                listener_offset = base->member_displacement;
                break;
            }
        }
    }
    if (listener_offset)
        install(static_cast<std::byte*>(player) + listener_offset, 0x43475864);
}
} //namespace skeet::engine
