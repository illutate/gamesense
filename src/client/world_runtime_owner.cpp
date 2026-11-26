#include "skeet/client/world_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/config_storage/owner.hpp"
#include "skeet/effects/runtime_owner.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/source_initialization_imports.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
engine::DecodedOffset field(ClientRuntimeStorage& storage, std::uint32_t identity) {
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return {storage.offsets().globals, identity, row.xor_key_identity, row.initial_xor_key,
                    0};
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return {storage.offsets().globals, identity, row.xor_key_identity, row.initial_xor_key,
                    0};
    throw std::logic_error("World field has no recovered receive/datamap recipe");
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage, std::uint32_t identity) {
    return {storage.owner_registries().at(identity), storage.memory()};
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& configuration,
                              HistoryRuntimeOwner& history, VisualRuntimeOwner& visual,
                              effects::EffectsRuntimeOwner& effects,
                              engine::HookRuntimeOwner& hooks, WindowRuntimeOwner& window,
                              ConfigStorageOwner& presets, WorldRuntimeImports imports) {
    auto& data = storage.data();
    const auto core = engine.interfaces.core();
    if (&engine.variables.data() != &data || &configuration.storage() != &storage ||
        &history.storage() != &storage || &visual.storage() != &storage ||
        &effects.storage() != &storage || &hooks.storage() != &storage ||
        &window.storage() != &storage || &hooks.unload_state() != &window.unload_state() ||
        &presets.configuration() != &storage.configuration() ||
        &presets.native_storage() != &storage.config_storage() ||
        core.engine_client != data.native_cell<void*>(0x4346A514) ||
        core.entity_list != data.native_cell<void*>(0x4346A890) ||
        core.cvar != data.native_cell<void*>(0x4346A87C))
        throw std::invalid_argument(
            "World factories require the canonical engine, settings, history, effects, hook and window owners");
    if (&effects.world().settings != &configuration.effects_settings() ||
        &effects.view().settings != &configuration.effects_settings() ||
        &visual.game_system().shutdown.owners != &storage.owner_registries().at(0x43475B78))
        throw std::invalid_argument(
            "World factories require the same effects settings and initialized game-system context");
    if (!storage.offsets().words || storage.offsets().globals.pointers.empty() ||
        !data.native_cell<engine::MemberFunction<void*, const char*>>(0x4346A374) ||
        !data.native_cell<void*>(0x43467B04) || !core.cvar || !core.engine_client ||
        !core.entity_list)
        throw std::invalid_argument(
            "World factories require acquired netvars, HUD lookup, global clock and interfaces");
    if (!imports.proxy.crc32 || !imports.proxy.exchange || !imports.window.get_foreground_window ||
        !imports.window.open_mutex || !imports.window.close || !imports.window.delete_file ||
        !imports.window.time || !imports.window.default_window_procedure ||
        !imports.window.module_file_name || !imports.window.temp_path ||
        !imports.window.dos_to_nt_path || !imports.window.free_unicode || !imports.window.format ||
        !imports.window.append_wide || !imports.window.current_process_id)
        throw std::invalid_argument("World factories require their actual OS import boundaries");
    if (!data.native_cell<const std::byte*>(0x43467288) || !data.native_cell<void**>(0x43468B2C))
        throw std::invalid_argument(
            "World proxy requires the proven engine guard and live game-interface slot");
    return storage;
}
}
WorldRuntimeImports native_world_runtime_imports() {
    return {input::native_window_lifecycle_imports(), engine::native_game_window_proxy_imports()};
}

struct WorldRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    rage::LiveTargetRelationships relationships;
    effects::HarmlessMolotovBindings molotov;
    events::HitMarkerBindings marker_events;
    float marker_remaining = 0;
    misc::NativeText marker_sound{};
    misc::PersistentKillFeedBindings kill_feed;
    WorldRuntimeFactoryBindings factories;
    GameWindowProxyFactoryBindings proxy;

    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& configuration, HistoryRuntimeOwner& history,
         VisualRuntimeOwner& visual, effects::EffectsRuntimeOwner& effects,
         engine::HookRuntimeOwner& hooks, WindowRuntimeOwner& window, ConfigStorageOwner& presets,
         WorldRuntimeImports imports)
        : storage(checked(source, engine, configuration, history, visual, effects, hooks, window,
                          presets, imports)),
          relationships{source.data().native_cell<void*>(0x4346A890), history.game_type(),
                        history.teammates_are_enemies(), history.players().adjustments(),
                        history.target_offsets()},
          molotov{source.data().native_cell<void*>(0x4346A514),
                  source.data().native_cell<void*>(0x4346A890),
                  &source.data().native_cell<void*>(0x43475808),
                  std::uint32_t(std::ptrdiff_t(field(source, 0x43467FCC))),
                  source.data().native_cell<std::uint32_t>(0x43467C3C),
                  source.data().native_cell<std::uint32_t>(0x434685C8),
                  relationships,
                  hooks.registry(),
                  {&hooks.descriptor_storage().at(0x43475908),
                   &hooks.descriptor_storage().at(0x434758F4),
                   &hooks.descriptor_storage().at(0x434757F4)},
                  source.data().native_cell<effects::HarmlessMolotovRuntime*>(0x43475828)},
          marker_events{source.data().native_cell<void*>(0x4346A514),
                        source.data().native_cell<void*>(0x4346A890),
                        source.data().native_cell<void*>(0x434689C0),
                        relationships,
                        source.data().native_cell<std::uint8_t>(0x43468E58),
                        source.data().native_cell<std::int32_t*>(0x4346D558)},
          kill_feed{
              source.data().native_cell<void*>(0x4346A514),
              source.data().native_cell<void*>(0x43468994),
              source.data().native_cell<engine::MemberFunction<void*, const char*>>(0x4346A374),
              source.data().native_cell<void* const*>(0x4346A3A4),
              source.data().native_cell<void*>(0x43467B04),
              {field(source, 0x4346746C), field(source, 0x43468BE4), field(source, 0x43468D6C)}},
          factories{
              registry(source, 0x43475B78),
              registry(source, 0x43475988),
              registry(source, 0x43475A9C),
              registry(source, 0x43475AD8),
              registry(source, 0x434758E0),
              registry(source, 0x43475B94),
              source.command_phases(),
              source.configuration(),
              effects.smoke_bindings(),
              effects.scope_bindings(),
              visual.game_system_factory(),
              hooks.auto_accept_owner_bindings(),
              {configuration.miscellaneous().reveal_ranks,
               source.data().native_cell<misc::RevealRanksRuntime*>(0x434758F0),
               {source.data().native_cell<void*>(0x4346A87C),
                source.data().native_cell<void*>(0x4346D664),
                source.data().native_cell<void* const*>(0x43468250),
                source.data().native_cell<void* const*>(0x4346A3A4), field(source, 0x43468378)}},
              {configuration.miscellaneous().reveal_overwatch,
               source.data().native_cell<void*>(0x4346A514)},
              {configuration.miscellaneous().disable_pure,
               source.data().native_cell<misc::DisableSvPureOwner*>(0x43475974)},
              {&source.data().native_cell<engine::GameSystemRegistrationNode**>(0x43468D50),
               source.owner_registries().at(0x43475A84), window.unload_state()},
              {configuration.miscellaneous().persistent_kill_feed, kill_feed},
              {source.event_registry(),
               {configuration.player_esp_settings().hit_marker, marker_remaining, marker_sound,
                marker_events, source.data().native_cell<void*>(0x43467B04),
                field(source, 0x43468E78)}},
              {presets.persistence(), imports.window, window.startup_imports().modules,
               window.startup_imports().images},
              {configuration.effects_settings(), molotov}},
          proxy{source.data_owner(), source.owner_registries(), source.memory(),
                engine::native_game_window_proxy_bindings(source.data(), imports.proxy)} {}
};
WorldRuntimeOwner::WorldRuntimeOwner(ClientRuntimeStorage& storage,
                                     const EngineRuntimeBindings& engine,
                                     ConfigurationRuntimeOwner& configuration,
                                     HistoryRuntimeOwner& history, VisualRuntimeOwner& visual,
                                     effects::EffectsRuntimeOwner& effects,
                                     engine::HookRuntimeOwner& hooks, WindowRuntimeOwner& window,
                                     ConfigStorageOwner& presets)
    : WorldRuntimeOwner(storage, engine, configuration, history, visual, effects, hooks, window,
                        presets, native_world_runtime_imports()) {}
WorldRuntimeOwner::WorldRuntimeOwner(ClientRuntimeStorage& storage,
                                     const EngineRuntimeBindings& engine,
                                     ConfigurationRuntimeOwner& configuration,
                                     HistoryRuntimeOwner& history, VisualRuntimeOwner& visual,
                                     effects::EffectsRuntimeOwner& effects,
                                     engine::HookRuntimeOwner& hooks, WindowRuntimeOwner& window,
                                     ConfigStorageOwner& presets, WorldRuntimeImports imports)
    : data_(std::make_unique<Data>(storage, engine, configuration, history, visual, effects, hooks,
                                   window, presets, imports)) {}
WorldRuntimeOwner::~WorldRuntimeOwner() = default;
ClientRuntimeStorage& WorldRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
WorldRuntimeFactoryBindings& WorldRuntimeOwner::factory_bindings() noexcept {
    return data_->factories;
}
GameWindowProxyFactoryBindings& WorldRuntimeOwner::proxy_factory_bindings() noexcept {
    return data_->proxy;
}
const effects::HarmlessMolotovBindings& WorldRuntimeOwner::molotov_bindings() const noexcept {
    return data_->molotov;
}
events::HitMarkerBindings& WorldRuntimeOwner::hit_marker_bindings() noexcept {
    return data_->marker_events;
}
void WorldRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    bind_world_runtime_factories(sources, data_->factories);
    bind_game_window_proxy_factory(sources, data_->proxy);
}
} //namespace skeet::client
