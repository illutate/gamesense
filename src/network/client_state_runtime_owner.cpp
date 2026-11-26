#include "skeet/network/client_state_runtime_owner.hpp"
#include "skeet/network/client_state_hook_bindings.hpp"
#include "skeet/network/client_state_hook_family.hpp"
#include "skeet/network/tick_update_dispatch.hpp"
#include "skeet/network/shared_esp.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/startup_bindings.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/events/event_hook.hpp"
#include "skeet/esp/other_config.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/menu/root_menu.hpp"
#include "skeet/menu/startup_window.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/script/owner.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::network {
namespace {
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t identity) {
    auto* pointer = data.native_cell<T*>(identity);
    if (!pointer)
        throw std::invalid_argument(
            "ClientState contexts require the initialized live engine alias");
    return *pointer;
}
engine::DecodedOffset rule_guard(client::ClientRuntimeStorage& storage) {
    return {storage.offsets().globals, 0x43468378, 0x43468244, 0xBF567619, 0};
}
misc::ErrorLog& errors(script::SourceScriptRuntimeOwner& scripts) {
    auto* log = scripts.module_bindings().client.error_log;
    if (!log)
        throw std::invalid_argument(
            "ClientState contexts require the actual shared script error log");
    return *log;
}
client::ClientRuntimeStorage&
checked_storage(client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
                client::ConfigurationRuntimeOwner& configuration,
                client::HistoryRuntimeOwner& history, client::CommandCaptureRuntimeOwner& commands,
                engine::HookRuntimeOwner& hooks, script::SourceScriptRuntimeOwner& scripts,
                ui::MainMenuWindowOwner& window, ClientStateRuntimeImports imports) {
    auto& data = storage.data();
    const auto core = engine.interfaces.core();
    auto& modules = scripts.module_bindings();
    auto& menu = window.controller();
    if (&configuration.storage() != &storage || &history.storage() != &storage ||
        &commands.storage() != &storage || &hooks.storage() != &storage ||
        &scripts.storage() != &storage || &history.players().storage() != &storage ||
        &scripts.unload_state() != &hooks.unload_state() ||
        &modules.player_cache != &history.players().cache() ||
        &modules.players != &history.players().adjustments() ||
        &modules.native_offsets != &storage.offsets() || &engine.variables.data() != &data ||
        &menu.script_owner() != &scripts.require_owner() ||
        &scripts.require_owner() != &hooks.script_owner() || &menu.window() != &window.window() ||
        data.native_cell<script::ScriptOwner*>(0x43475B00) != &scripts.require_owner() ||
        core.engine_client != data.native_cell<void*>(0x4346A514) ||
        core.entity_list != data.native_cell<void*>(0x4346A890) ||
        &commands.sent_history().angles != &data.native_cell<std::array<float, 3>>(0x43476414) ||
        &commands.sent_history().records !=
            &data.native_cell<std::array<client::SentCommandRecord, 150>>(0x43476450))
        throw std::invalid_argument(
            "ClientState contexts must share the actual menu, script, history and hook owners");
    if (!imports.exchange || !core.engine_client || !core.entity_list ||
        !data.native_cell<void*>(0x43468E44) || !data.native_cell<std::byte*>(0x43468D20) ||
        !engine.variables.at(0x43469238).get())
        throw std::invalid_argument(
            "ClientState contexts require their genuine engine objects, ConVar and exchange import");
    (void)hooks.channel();
    (void)errors(scripts);
    (void)std::ptrdiff_t(rule_guard(storage));
    if (&history.entity_runtime().interval != &pointed<float>(data, 0x4346A338))
        throw std::invalid_argument(
            "ClientState and history must share the same live engine interval");
    return storage;
}
}
ClientStateRuntimeImports native_client_state_runtime_imports() {
    const auto module = GetModuleHandleW(L"kernel32.dll");
    const auto exchange = module ? GetProcAddress(module, "InterlockedExchange") : nullptr;
    if (!exchange)
        throw std::runtime_error(
            "ClientState contexts require the genuine InterlockedExchange export");
    return {reinterpret_cast<decltype(ClientStateRuntimeImports::exchange)>(exchange)};
}
struct ClientStateRuntimeOwner::Data {
    client::ClientRuntimeStorage& storage;
    client::ConfigurationRuntimeOwner& configuration;
    client::PostInitializationData startup;
    ClientStateServerCvars cvars;
    ClientStateHooksBindings core_bindings;
    ClientStateHooksRuntime core;
    TickUpdateRuntime ticks;
    IncomingUpdateRuntime incoming;
    SharedEspRuntime shared;
    events::EventPacketHookRuntime events;
    client::EntityUpdateRuntime entities;
    ClientStateHookFamily family;
    Data(client::ClientRuntimeStorage& source, const client::EngineRuntimeBindings& engine,
         client::ConfigurationRuntimeOwner& settings, client::HistoryRuntimeOwner& history,
         client::CommandCaptureRuntimeOwner& commands, engine::HookRuntimeOwner& hooks,
         script::SourceScriptRuntimeOwner& scripts, ui::MainMenuWindowOwner& window,
         ClientStateRuntimeImports imports)
        : storage(checked_storage(source, engine, settings, history, commands, hooks, scripts,
                                  window, imports)),
          configuration(settings), startup(source.data_owner()),
          cvars{scripts.bridge(), source.data().native_cell<std::byte*>(0x43468D20),
                source.data().native_cell<ServerCvarNameGetter>(0x4347CA4C),
                source.data().native_cell<std::uint8_t>(0x4347CA50),
                ServerCvarEventNames(source.event_names())},
          core_bindings{source.data().native_cell<void*>(0x4346A514),
                        hooks.channel_state(),
                        cvars,
                        source.data().native_cell<ClientStateScopedPatch*>(0x43476900),
                        engine.variables.at(0x43469238),
                        engine.variables.startup_variables(),
                        source.data().native_cell<float*>(0x4346A338),
                        source.data().native_cell<std::int32_t>(0x4347C9EC),
                        source.data().native_cell<float>(0x43469254),
                        startup.acknowledgements(),
                        startup.command_reset(),
                        source.owner_registries().at(0x43475B78),
                        settings.anti_untrusted().enabled.value,
                        source.data().native_cell<void**>(0x4346A3A4),
                        std::ptrdiff_t(rule_guard(source)),
                        window.controller().preset_manager(),
                        errors(scripts)},
          core(core_bindings),
          ticks{{source.entity_observers(),
                 source.data().native_cell<IncomingUpdatePatch*>(0x4347644C),
                 source.data().native_cell<std::int32_t>(0x4346D4B4),
                 source.data().native_cell<std::int32_t>(0x4346D4B8),
                 source.data().native_cell<std::int32_t>(0x4347C9EC),
                 source.data().native_cell<float>(0x43479924),
                 source.data().native_cell<float>(0x43476928),
                 source.data().native_cell<const std::int32_t*>(0x434758D8),
                 source.data().native_cell<const float*>(0x4346A334)},
                {source.data().native_cell<void*>(0x4346A514),
                 source.data().native_cell<void* const*>(0x4346A3A4), rule_guard(source),
                 startup.acknowledgements(), hooks.channel().bindings.history, imports.exchange}},
          incoming{{source.data().native_cell<void*>(0x43475808),
                    source.data().native_cell<const esp::ActiveSoundTree*>(0x43476420),
                    source.data().native_cell<void*>(0x4346A514),
                    source.data().native_cell<void*>(0x4346A890),
                    source.data().native_cell<void*>(0x43467B04),
                    source.data().native_cell<client::EntityCommandOwner*>(0x43475898),
                    source.data().native_cell<std::int32_t*>(0x43475BA4),
                    source.data().native_cell<const std::int32_t*>(0x43475794),
                    source.data().native_cell<const std::int32_t*>(0x434758D8),
                    source.data().native_cell<const std::int32_t*>(0x43475A60),
                    source.data().native_cell<const float*>(0x4346A338),
                    source.data().native_cell<std::int32_t>(0x4346D4B4),
                    source.data().native_cell<std::int32_t>(0x4346D4B8),
                    source.data().native_cell<std::int32_t>(0x4347C9EC),
                    source.data().native_cell<lagcomp::TickTimingSamples>(0x43478C70),
                    source.data().native_cell<float>(0x43476928), source.entity_observers(),
                    source.data().native_cell<IncomingUpdatePatch*>(0x4347644C)},
                   {engine.variables.at(0x43469238), engine.variables.at(0x4346A850),
                    commands.player_gates().rejection_bytes[0], commands.player_gates().tick_base,
                    commands.sent_history(), hooks.channel().bindings.history,
                    source.data().native_cell<esp::EspOwnerRuntime*>(0x434758DC),
                    imports.exchange}},
          shared{source.data().native_cell<client::RejectedPlayerUpdateOwner*>(0x434758D0),
                 source.data().native_cell<void*>(0x43475808),
                 source.data().native_cell<void*>(0x4346A514),
                 source.data().native_cell<void*>(0x4346A890),
                 source.data().native_cell<void**>(0x43468FF0),
                 source.data().native_cell<void**>(0x4346A3A4),
                 history.game_type(),
                 history.teammates_are_enemies(),
                 history.players().adjustments(),
                 history.players().cache(),
                 settings.other_esp_settings().shared.enabled.value,
                 settings.other_esp_settings().shared.restrict_updates.value,
                 pointed<float>(source.data(), 0x4346A338),
                 reinterpret_cast<const float*>(source.data().native_cell<void*>(0x43467B04))[8],
                 scripts.bridge(),
                 source.event_names().alias(events::EventName::voice),
                 shared_esp_offsets(source.offsets())},
          events(source.event_registry(), source.owner_registries().at(0x434758E0),
                 &source.data().native_cell<void*>(0x43475808)),
          entities{{source.data().native_cell<std::uint32_t>(0x43476904),
                    source.data().native_cell<client::EntityUpdatePatch*>(0x43476908),
                    source.data().native_cell<std::int32_t>(0x4346D4BC), source.entity_observers()},
                   {source.data().native_cell<void*>(0x4346A514),
                    source.data().native_cell<void*>(0x4346A890),
                    std::uint32_t(history.target_offsets().team), imports.exchange},
                   window.controller().skins_menu()},
          family(hooks.descriptor_storage(), hooks.registry(), hooks.descriptor_images(),
                 hooks.descriptor_system(),
                 {core, ticks, incoming, settings.effects_settings(), shared, events, entities}) {}
};
ClientStateRuntimeOwner::ClientStateRuntimeOwner(
    client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
    client::ConfigurationRuntimeOwner& configuration, client::HistoryRuntimeOwner& history,
    client::CommandCaptureRuntimeOwner& commands, engine::HookRuntimeOwner& hooks,
    script::SourceScriptRuntimeOwner& scripts, ui::MainMenuWindowOwner& window)
    : ClientStateRuntimeOwner(storage, engine, configuration, history, commands, hooks, scripts,
                              window, native_client_state_runtime_imports()) {}
ClientStateRuntimeOwner::ClientStateRuntimeOwner(
    client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
    client::ConfigurationRuntimeOwner& configuration, client::HistoryRuntimeOwner& history,
    client::CommandCaptureRuntimeOwner& commands, engine::HookRuntimeOwner& hooks,
    script::SourceScriptRuntimeOwner& scripts, ui::MainMenuWindowOwner& window,
    ClientStateRuntimeImports imports)
    : data_(std::make_unique<Data>(storage, engine, configuration, history, commands, hooks,
                                   scripts, window, imports)) {}
ClientStateRuntimeOwner::~ClientStateRuntimeOwner() = default;
client::ClientRuntimeStorage& ClientStateRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
ClientStateHooksRuntime& ClientStateRuntimeOwner::core() noexcept {
    return data_->core;
}
TickUpdateRuntime& ClientStateRuntimeOwner::ticks() noexcept {
    return data_->ticks;
}
IncomingUpdateRuntime& ClientStateRuntimeOwner::incoming() noexcept {
    return data_->incoming;
}
SharedEspRuntime& ClientStateRuntimeOwner::shared_esp() noexcept {
    return data_->shared;
}
events::EventPacketHookRuntime& ClientStateRuntimeOwner::event_packets() noexcept {
    return data_->events;
}
client::EntityUpdateRuntime& ClientStateRuntimeOwner::entities() noexcept {
    return data_->entities;
}
const esp::EffectsSettings& ClientStateRuntimeOwner::effects() const noexcept {
    return data_->configuration.effects_settings();
}
ClientStateHookFamily& ClientStateRuntimeOwner::family() noexcept {
    return data_->family;
}
}
