#include "skeet/client/shared_esp_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/rage/target_relationships.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t cell) {
    auto* pointer = data.native_cell<T*>(cell);
    if (!pointer)
        throw std::invalid_argument("Shared ESP requires a genuine initialized live engine alias");
    return *pointer;
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& configuration, PlayerRuntimeOwner& players,
                              HistoryRuntimeOwner& history,
                              script::SourceScriptRuntimeOwner& scripts) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &configuration.storage() != &storage ||
        &players.storage() != &storage || &history.storage() != &storage ||
        &history.players() != &players || &scripts.storage() != &storage)
        throw std::invalid_argument(
            "Shared ESP must borrow the canonical configuration, player, history and script owners");
    if (data.native_cell<RejectedPlayerUpdateOwner*>(0x434758D0))
        throw std::logic_error(
            "Shared ESP contexts must be composed before their actual feature factory");
    const auto* globals = data.native_cell<const std::byte*>(0x43467B04);
    if (!globals ||
        reinterpret_cast<const std::byte*>(data.native_cell<float*>(0x4346A334)) != globals ||
        reinterpret_cast<const std::byte*>(data.native_cell<float*>(0x4346A338)) !=
            globals + 0x20 ||
        reinterpret_cast<const std::byte*>(data.native_cell<std::int32_t*>(0x43469300)) !=
            globals + 0x18 ||
        !data.native_cell<const std::int32_t*>(0x434757DC) ||
        !data.native_cell<void**>(0x43468FF0) || !data.native_cell<void**>(0x4346A3A4))
        throw std::invalid_argument(
            "Shared ESP requires the actual clock, choked-command, resource and game-rules aliases");
    const auto core = engine.interfaces.core();
    if (!core.engine_client || core.engine_client != data.native_cell<void*>(0x4346A514) ||
        !core.entity_list || core.entity_list != data.native_cell<void*>(0x4346A890))
        throw std::invalid_argument(
            "Shared ESP interfaces do not belong to the canonical engine bundle");
    for (const auto cell :
         {0x43469068u, 0x43468D88u, 0x43467A8Cu, 0x43467A4Cu, 0x43468EA8u, 0x4346A568u})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument("Shared ESP requires its actual acquired ConVars");
    if (!data.native_cell<const std::byte*>(0x4346D668) ||
        !data.native_cell<const std::byte*>(0x4346D66C))
        throw std::invalid_argument(
            "Shared ESP requires the verified engine message CALL producers before construction");
    return storage;
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage, std::uint32_t cell) {
    return {storage.owner_registries().at(cell), storage.memory()};
}
RejectedPlayerUpdateOffsets fields(ClientRuntimeStorage& storage) {
    const auto& g = storage.offsets().globals;
    return {engine::DecodedOffset(g, 0x43468BF8, 0x43467B8C, 0xD070684C, 0),
            engine::DecodedOffset(g, 0x43468E78, 0x434684E4, 0x9D920E35, 0),
            engine::DecodedOffset(g, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0),
            engine::DecodedOffset(g, 0x43467910, 0x43467C08, 0xD720AEA6, 0),
            engine::DecodedOffset(g, 0x43468D6C, 0x43468428, 0x9F8E7F71, 0)};
}
}
struct SharedEspRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    RejectedPlayerUpdateOwner state;
    SharedEspPacketCipher cipher = SharedEspPacketCipher::legacy_xor;
    rage::LiveTargetRelationships relationships;
    RejectedPlayerUpdateContext rejected;
    SharedEspCommandOwner command;
    network::SharedEspRuntime incoming;
    SharedEspConstructorGlobals globals;
    SharedEspOwnerRegistries registries;
    SharedEspInitialization initialization;

    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& configuration, PlayerRuntimeOwner& players,
         HistoryRuntimeOwner& history, script::SourceScriptRuntimeOwner& scripts)
        : storage(checked(source, engine, configuration, players, history, scripts)),
          relationships{source.data().native_cell<void*>(0x4346A890),
                        engine.variables.at(0x43467A8C), engine.variables.at(0x4346A568),
                        players.adjustments(), history.target_offsets()},
          rejected{source.data().native_cell<RejectedPlayerUpdateOwner*>(0x434758D0),
                   configuration.other_esp_settings().shared,
                   source.input_runtime().bitmap(),
                   players.cache(),
                   players.adjustments(),
                   relationships,
                   source.data().native_cell<void*>(0x4346A890),
                   source.data().native_cell<void**>(0x4346A3A4),
                   source.data().native_cell<const std::int32_t*>(0x434757DC),
                   pointed<std::int32_t>(source.data(), 0x43469300),
                   pointed<float>(source.data(), 0x4346A334),
                   pointed<float>(source.data(), 0x4346A338),
                   *reinterpret_cast<const float*>(
                       source.data().native_cell<const std::byte*>(0x43467B04) + 0x20),
                   cipher,
                   fields(source)},
          command{rejected},
          incoming{source.data().native_cell<RejectedPlayerUpdateOwner*>(0x434758D0),
                   source.data().native_cell<void*>(0x43475808),
                   source.data().native_cell<void*>(0x4346A514),
                   source.data().native_cell<void*>(0x4346A890),
                   source.data().native_cell<void**>(0x43468FF0),
                   source.data().native_cell<void**>(0x4346A3A4),
                   engine.variables.at(0x43467A8C),
                   engine.variables.at(0x4346A568),
                   players.adjustments(),
                   players.cache(),
                   configuration.other_esp_settings().shared.enabled.value,
                   configuration.other_esp_settings().shared.restrict_updates.value,
                   pointed<float>(source.data(), 0x4346A338),
                   *reinterpret_cast<const float*>(
                       source.data().native_cell<const std::byte*>(0x43467B04) + 0x20),
                   scripts.bridge(),
                   source.event_names().alias(events::EventName::voice),
                   network::shared_esp_offsets(source.offsets())},
          globals{source.data().native_cell<void*>(0x4346A514),
                  source.data().native_cell<std::uint32_t>(0x43469068),
                  source.data().native_cell<std::uint32_t>(0x43468D88),
                  source.data().native_cell<std::uint32_t>(0x43467A8C),
                  source.data().native_cell<std::uint32_t>(0x43467A4C),
                  source.data().native_cell<std::uint32_t>(0x43468EA8),
                  source.data().native_cell<const std::byte*>(0x4346D668),
                  source.data().native_cell<const std::byte*>(0x4346D66C)},
          registries{registry(source, 0x43475B78), source.command_phases(),
                     registry(source, 0x43475B94), source.entity_observers(),
                     source.empty_ticks(),         source.event_registry()},
          initialization{registries,
                         source.configuration(),
                         {state, configuration.other_esp_settings().shared, command, incoming,
                          source.data().native_cell<RejectedPlayerUpdateOwner*>(0x434758D0),
                          globals}} {}
};
SharedEspRuntimeOwner::SharedEspRuntimeOwner(ClientRuntimeStorage& storage,
                                             const EngineRuntimeBindings& engine,
                                             ConfigurationRuntimeOwner& configuration,
                                             PlayerRuntimeOwner& players,
                                             HistoryRuntimeOwner& history,
                                             script::SourceScriptRuntimeOwner& scripts)
    : data_(std::make_unique<Data>(storage, engine, configuration, players, history, scripts)) {}
SharedEspRuntimeOwner::~SharedEspRuntimeOwner() = default;
ClientRuntimeStorage& SharedEspRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
SharedEspInitialization& SharedEspRuntimeOwner::initializers() noexcept {
    return data_->initialization;
}
const RejectedPlayerUpdateContext& SharedEspRuntimeOwner::rejected_player_update() const noexcept {
    return data_->rejected;
}
network::SharedEspRuntime& SharedEspRuntimeOwner::incoming() noexcept {
    return data_->incoming;
}
} // namespace skeet::client
