#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/static_cleanup.hpp"
#include "skeet/client/exit_registration.hpp"
#include "skeet/client/hook_descriptor_cleanup.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/rage/group_storage.hpp"
#include "skeet/rage/spread_storage.hpp"
#include "skeet/esp/flag_table_initializer.hpp"
#include "skeet/script/ui_type_storage.hpp"
#include "skeet/events/event_name_storage.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/client/config_initializers.hpp"
#include "skeet/client/global_config_initializers.hpp"
#include "skeet/client/target_mask_owner.hpp"
#include "skeet/anti_aim/startup_initializers.hpp"
#include "skeet/esp/config.hpp"
#include "skeet/esp/effects_config.hpp"
#include "skeet/esp/other_config.hpp"
#include "skeet/esp/model_color_config.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/misc/no_fall_damage.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/client/static_crt_initialization.hpp"
#include "skeet/engine/startup_vector_cleanup.hpp"
#include "skeet/engine/startup_container_cleanup.hpp"
#include "skeet/render/renderer_assets.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/panorama_api.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {
struct RandomImports {
    void(__cdecl* seed)(std::int32_t);
    float(__cdecl* uniform)(float, float);
};
RandomImports required_random_imports() {
    const auto module = GetModuleHandleA("vstdlib.dll");
    if (!module)
        throw std::runtime_error("Spread RNG requires the already-loaded vstdlib.dll");
    const auto seed = GetProcAddress(module, "RandomSeed");
    const auto uniform = GetProcAddress(module, "RandomFloat");
    if (!seed || !uniform)
        throw std::runtime_error("vstdlib RandomSeed/RandomFloat exports are missing");
    return {reinterpret_cast<decltype(RandomImports::seed)>(seed),
            reinterpret_cast<decltype(RandomImports::uniform)>(uniform)};
}
void seed_random(void* context, std::int32_t seed) {
    static_cast<const RandomImports*>(context)->seed(seed);
}
float uniform_random(void* context, float minimum, float maximum) {
    return static_cast<const RandomImports*>(context)->uniform(minimum, maximum);
}
CrtRuntimeImports require_imports(ClientRuntimeStorage& storage, CrtRuntimeImports imports) {
    const auto memory = storage.memory();
    if (!memory.allocate || !memory.free || !memory.process_heap ||
        imports.startup.memory.allocate != memory.allocate ||
        imports.startup.memory.free != memory.free ||
        imports.startup.memory.process_heap != memory.process_heap ||
        imports.startup.memory.purecall != memory.purecall)
        throw std::invalid_argument("CRT runtime must share the canonical process-heap imports");
    if (!imports.random.seed || !imports.random.uniform || !imports.trigonometry.sine ||
        !imports.trigonometry.cosine)
        throw std::invalid_argument(
            "CRT runtime requires its actual RNG and SSE trigonometry imports");
    if (!imports.hook_queries.query_memory || !imports.startup.images.image_nt_header ||
        !imports.startup.modules.loader ||
        !imports.startup.forwarded_exports.image_directory_entry ||
        !imports.startup.terminate_process)
        throw std::invalid_argument(
            "CRT runtime requires its actual loader, image, query and termination imports");
    return imports;
}
}

const rage::SpreadRandom& native_spread_random() {
    static RandomImports imports = required_random_imports();
    static const rage::SpreadRandom random{&imports, seed_random, uniform_random};
    return random;
}
CrtRuntimeImports native_crt_runtime_imports() {
    return {engine::native_source_initialization_imports(), engine::native_hook_descriptor_system(),
            native_spread_random(), animation::native_sse_trigonometry()};
}

struct CrtRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    CrtRuntimeImports imports;
    rage::SpreadStorage spread;
    rage::SpreadStorageBindings spread_imports;
    StaticCleanupRuntime static_cleanup;
    ExitRegistrationContext exits;
    engine::HookDescriptorStorage descriptor_storage;
    engine::HookDescriptorRuntime descriptors;
    HookDescriptorCleanupContext descriptor_initializers;
    rage::GroupStorage groups;
    rage::GroupInitializerBindings group_initializers;
    esp::FlagTable flags;
    esp::FlagTableInitialization flag_initializers;
    script::UiTypeStorage ui_types;
    script::UiTypeInitialization ui_type_initializers;
    events::EventNameStartupBindings event_names;
    engine::FirstStartupServiceBindings first_startup;

    Data(ClientRuntimeStorage& source, CrtRuntimeImports services)
        : storage(source), imports(require_imports(source, services)), spread(source.data_owner()),
          spread_imports{source.memory(), imports.random, imports.trigonometry},
          static_cleanup({source.owner_registries(), source.data_update_observers(),
                          source.recharge_observers(), source.prediction_observers(),
                          source.empty_ticks(), source.command_phases(), source.entity_observers(),
                          spread, spread_imports, source.config_storage(), source.memory()}),
          exits(source.exit_callbacks(), static_cleanup), descriptor_storage(source.data_owner()),
          descriptors(source.memory(), imports.hook_queries, imports.startup.images),
          descriptor_initializers(source.exit_callbacks(), descriptors, descriptor_storage),
          groups(source.data_owner(), source.memory()),
          group_initializers{source.configuration(), source.exit_callbacks(), groups},
          flags(source.data_owner(), source.memory()),
          flag_initializers(flags, source.exit_callbacks()),
          ui_types(source.data_owner(), source.memory()),
          ui_type_initializers(ui_types, source.exit_callbacks()),
          event_names(source.event_registry(), source.exit_callbacks(), source.memory()),
          first_startup(imports.startup.first_startup_bindings(source.data())) {}
};

CrtRuntimeOwner::CrtRuntimeOwner(ClientRuntimeStorage& storage)
    : CrtRuntimeOwner(storage, native_crt_runtime_imports()) {}
CrtRuntimeOwner::CrtRuntimeOwner(ClientRuntimeStorage& storage, CrtRuntimeImports imports)
    : data_(std::make_unique<Data>(storage, imports)) {}
CrtRuntimeOwner::~CrtRuntimeOwner() = default;
ClientRuntimeStorage& CrtRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
const rage::SpreadRandom& CrtRuntimeOwner::random() const noexcept {
    return data_->imports.random;
}
const RageTrigonometry& CrtRuntimeOwner::trigonometry() const noexcept {
    return data_->imports.trigonometry;
}
rage::SpreadStorage& CrtRuntimeOwner::spread() noexcept {
    return data_->spread;
}
const rage::SpreadStorageBindings& CrtRuntimeOwner::spread_bindings() const noexcept {
    return data_->spread_imports;
}
StaticCleanupRuntime& CrtRuntimeOwner::static_cleanup() noexcept {
    return data_->static_cleanup;
}
ExitRegistrationContext& CrtRuntimeOwner::exit_registration() noexcept {
    return data_->exits;
}
engine::HookDescriptorStorage& CrtRuntimeOwner::descriptor_storage() noexcept {
    return data_->descriptor_storage;
}
engine::HookDescriptorRuntime& CrtRuntimeOwner::descriptors() noexcept {
    return data_->descriptors;
}
engine::HookDescriptorSystem CrtRuntimeOwner::hook_system() const noexcept {
    return data_->imports.hook_queries;
}
HookDescriptorCleanupContext& CrtRuntimeOwner::descriptor_initializers() noexcept {
    return data_->descriptor_initializers;
}
rage::GroupStorage& CrtRuntimeOwner::rage_groups() noexcept {
    return data_->groups;
}
rage::GroupInitializerBindings& CrtRuntimeOwner::group_initializers() noexcept {
    return data_->group_initializers;
}
esp::FlagTable& CrtRuntimeOwner::esp_flags() noexcept {
    return data_->flags;
}
esp::FlagTableInitialization& CrtRuntimeOwner::flag_initializers() noexcept {
    return data_->flag_initializers;
}
script::UiTypeStorage& CrtRuntimeOwner::ui_types() noexcept {
    return data_->ui_types;
}
script::UiTypeInitialization& CrtRuntimeOwner::ui_type_initializers() noexcept {
    return data_->ui_type_initializers;
}
events::EventNameStartupBindings& CrtRuntimeOwner::event_name_initializers() noexcept {
    return data_->event_names;
}
engine::FirstStartupServiceBindings& CrtRuntimeOwner::first_startup_services() noexcept {
    return data_->first_startup;
}

void CrtRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("CRT callbacks must share the canonical source data owner");
    bind_exit_initializers(sources, exit_registration());
    bind_hook_descriptor_initializers(sources, descriptor_initializers());
    rage::bind_native_group_initializers(sources, group_initializers());
    esp::bind_flag_table_initializer(sources, flag_initializers());
    script::bind_ui_type_initializer(sources, ui_type_initializers());
    events::bind_event_name_initializer(sources, event_name_initializers());
}

struct ConfigurationRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    MovementGlobalConfig movement;
    skins::Settings skins;
    rage::Settings rage;
    anti_aim::Config anti_aim;
    anti_aim::GlobalConfig anti_aim_global;
    anti_aim::FakeLagSettings fake_lag;
    anti_aim::OtherGlobalSettings anti_aim_other;
    esp::PlayerEspSettings player_esp;
    esp::OtherEspSettings other_esp;
    esp::EffectsSettings effects;
    esp::ColoredModelsSettings colored_models;
    misc::FovConfig fov;
    misc::KnifeConfig knife;
    misc::ZeusConfig zeus;
    misc::AutomaticWeaponsConfig automatic_weapons;
    misc::QuickSwitchConfig quick_switch;
    misc::RevealRanksConfig reveal_ranks;
    misc::RevealOverwatchConfig reveal_overwatch;
    misc::ClanTagConfig clan_tag;
    misc::EventLogConfig event_log;
    misc::GrenadePredictionConfig grenades;
    misc::PersistentKillFeedConfig persistent_kill_feed;
    misc::DisablePureConfig disable_pure;
    misc::RebuyConfig rebuy;
    misc::ConsoleOutputConfig console_output;
    misc::NoFallDamageConfig no_fall_damage;
    MiscFeatureSettings miscellaneous;
    misc::PingSpikeConfig ping_spike;
    misc::FreeLookConfig& free_look;
    misc::AntiUntrustedConfig& anti_untrusted;
    misc::HideFromObsConfig& hide_from_obs;
    misc::CustomEventsConfig& custom_events;
    misc::AutoBuyConfig auto_buy;
    TargetMaskSettings& target_mask;
    ui::WidgetIdentities identities;
    anti_aim::StartupInitializerBindings anti_aim_initializers;
    ConfigInitializerBindings static_initializers;
    GlobalConfigInitializerBindings global_initializers;

    explicit Data(ClientRuntimeStorage& source)
        : storage(source), miscellaneous{fov,
                                         knife,
                                         zeus,
                                         automatic_weapons,
                                         quick_switch,
                                         reveal_ranks,
                                         reveal_overwatch,
                                         clan_tag,
                                         event_log,
                                         grenades,
                                         persistent_kill_feed,
                                         disable_pure,
                                         rebuy,
                                         console_output,
                                         no_fall_damage},
          free_look(source.data().native_cell<misc::FreeLookConfig>(0x43468264u)),
          anti_untrusted(source.data().native_cell<misc::AntiUntrustedConfig>(0x434693DCu)),
          hide_from_obs(source.data().native_cell<misc::HideFromObsConfig>(0x43468E48u)),
          custom_events(source.data().native_cell<misc::CustomEventsConfig>(0x434691A8u)),
          target_mask(source.data().native_cell<TargetMaskSettings>(0x43467974u)),
          anti_aim_initializers{source.configuration(), fake_lag, anti_aim_other},
          static_initializers{source.configuration(),
                              other_esp,
                              player_esp,
                              effects,
                              rage.global,
                              ping_spike,
                              anti_aim_global,
                              target_mask.enabled},
          global_initializers{source.configuration(),
                              rage.global,
                              other_esp,
                              effects,
                              colored_models,
                              anti_untrusted,
                              hide_from_obs,
                              custom_events,
                              free_look,
                              source.settings_menu_storage()} {}
};

ConfigurationRuntimeOwner::ConfigurationRuntimeOwner(ClientRuntimeStorage& storage)
    : data_(std::make_unique<Data>(storage)) {}
ConfigurationRuntimeOwner::~ConfigurationRuntimeOwner() = default;
ClientRuntimeStorage& ConfigurationRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
MovementGlobalConfig& ConfigurationRuntimeOwner::movement() noexcept {
    return data_->movement;
}
skins::Settings& ConfigurationRuntimeOwner::skins_settings() noexcept {
    return data_->skins;
}
rage::Settings& ConfigurationRuntimeOwner::rage_settings() noexcept {
    return data_->rage;
}
anti_aim::Config& ConfigurationRuntimeOwner::anti_aim_settings() noexcept {
    return data_->anti_aim;
}
anti_aim::GlobalConfig& ConfigurationRuntimeOwner::anti_aim_global() noexcept {
    return data_->anti_aim_global;
}
anti_aim::FakeLagSettings& ConfigurationRuntimeOwner::fake_lag_settings() noexcept {
    return data_->fake_lag;
}
anti_aim::OtherGlobalSettings& ConfigurationRuntimeOwner::anti_aim_other() noexcept {
    return data_->anti_aim_other;
}
esp::PlayerEspSettings& ConfigurationRuntimeOwner::player_esp_settings() noexcept {
    return data_->player_esp;
}
esp::OtherEspSettings& ConfigurationRuntimeOwner::other_esp_settings() noexcept {
    return data_->other_esp;
}
esp::EffectsSettings& ConfigurationRuntimeOwner::effects_settings() noexcept {
    return data_->effects;
}
esp::ColoredModelsSettings& ConfigurationRuntimeOwner::colored_model_settings() noexcept {
    return data_->colored_models;
}
MiscFeatureSettings& ConfigurationRuntimeOwner::miscellaneous() noexcept {
    return data_->miscellaneous;
}
misc::PingSpikeConfig& ConfigurationRuntimeOwner::ping_spike() noexcept {
    return data_->ping_spike;
}
misc::FreeLookConfig& ConfigurationRuntimeOwner::free_look() noexcept {
    return data_->free_look;
}
misc::AntiUntrustedConfig& ConfigurationRuntimeOwner::anti_untrusted() noexcept {
    return data_->anti_untrusted;
}
misc::HideFromObsConfig& ConfigurationRuntimeOwner::hide_from_obs() noexcept {
    return data_->hide_from_obs;
}
misc::CustomEventsConfig& ConfigurationRuntimeOwner::custom_events() noexcept {
    return data_->custom_events;
}
misc::AutoBuyConfig& ConfigurationRuntimeOwner::auto_buy() noexcept {
    return data_->auto_buy;
}
TargetMaskSettings& ConfigurationRuntimeOwner::target_mask() noexcept {
    return data_->target_mask;
}
ui::WidgetIdentities& ConfigurationRuntimeOwner::widget_identities() noexcept {
    return data_->identities;
}
ui::SettingsMenuStorage& ConfigurationRuntimeOwner::settings_menu() noexcept {
    return storage().settings_menu_storage();
}
anti_aim::StartupInitializerBindings& ConfigurationRuntimeOwner::anti_aim_initializers() noexcept {
    return data_->anti_aim_initializers;
}
ConfigInitializerBindings& ConfigurationRuntimeOwner::static_initializers() noexcept {
    return data_->static_initializers;
}
GlobalConfigInitializerBindings& ConfigurationRuntimeOwner::global_initializers() noexcept {
    return data_->global_initializers;
}
void ConfigurationRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument(
            "Configuration callbacks must share the canonical source data owner");
    anti_aim::bind_native_startup_initializers(sources, anti_aim_initializers());
    bind_native_config_initializers(sources, static_initializers());
    bind_global_config_initializers(sources, global_initializers());
}

WindowRuntimeImports
native_window_runtime_imports(const engine::SourceInitializationImports& imports) {
    return {imports, engine::native_unload_system_functions(imports.common_controls),
            input::native_window_hook_imports(imports.common_controls)};
}
namespace {
WindowRuntimeImports require_window_imports(ClientRuntimeStorage& storage,
                                            WindowRuntimeImports imports) {
    const auto memory = storage.memory();
    if (imports.startup.memory.allocate != memory.allocate ||
        imports.startup.memory.free != memory.free ||
        imports.startup.memory.process_heap != memory.process_heap ||
        imports.unload.free_heap != memory.free ||
        imports.unload.process_heap != memory.process_heap)
        throw std::invalid_argument("Window runtime must share the canonical process-heap imports");
    const auto& modules = imports.startup.modules;
    const auto& unload = imports.unload;
    const auto& window = imports.subclass;
    const auto& controls = imports.startup.common_controls;
    if (!modules.loader || !modules.acquire_peb_lock || !modules.release_peb_lock ||
        !unload.set_window_long_w || !unload.set_window_long_a || !unload.get_window_long_w ||
        !unload.get_window_long_a || !unload.remove_window_subclass ||
        !window.read_timestamp_counter || !window.current_thread_id_slot ||
        !window.set_window_subclass || !window.default_subclass_proc || !window.get_window_thread ||
        !window.pc_to_file_header)
        throw std::invalid_argument("Window runtime requires its actual loader and window imports");
    if (window.set_window_subclass !=
            reinterpret_cast<input::WindowHookImports::SetSubclass>(controls.set_window_subclass) ||
        window.default_subclass_proc !=
            reinterpret_cast<input::WindowHookImports::DefaultProcedure>(
                controls.default_subclass_proc) ||
        unload.remove_window_subclass !=
            reinterpret_cast<engine::UnloadSystemFunctions::RemoveWindowSubclass>(
                controls.remove_window_subclass))
        throw std::invalid_argument(
            "Window runtime must share the resolved common-controls exports");
    return imports;
}
}
struct WindowRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    WindowRuntimeImports imports;
    engine::UnloadState state;
    engine::UnloadRuntime unload;
    input::WindowSubclassRuntime subclass;
    Data(ClientRuntimeStorage& source, WindowRuntimeImports services)
        : storage(source), imports(require_window_imports(source, services)),
          state(source.owner_registries().at(0x43475B78u),
                source.data().native_cell<bool>(0x43475895u),
                source.data().native_cell<std::uint32_t>(0x43476908u),
                source.data().native_cell<std::uint32_t>(0x4347644Cu),
                source.data().native_cell<void*>(0x4346A514u),
                source.data().native_cell<void (*)()>(0x43468BF0u),
                source.data().native_cell<bool>(0x43468B80u),
                source.data().native_cell<void*>(0x4346A470u)),
          unload(state, imports.unload),
          subclass(source.input_runtime(), unload, imports.startup.modules, imports.subclass) {}
};
WindowRuntimeOwner::WindowRuntimeOwner(ClientRuntimeStorage& storage)
    : WindowRuntimeOwner(storage, engine::native_source_initialization_imports()) {}
WindowRuntimeOwner::WindowRuntimeOwner(ClientRuntimeStorage& storage,
                                       const engine::SourceInitializationImports& imports)
    : WindowRuntimeOwner(storage, native_window_runtime_imports(imports)) {}
WindowRuntimeOwner::WindowRuntimeOwner(ClientRuntimeStorage& storage, WindowRuntimeImports imports)
    : data_(std::make_unique<Data>(storage, imports)) {}
WindowRuntimeOwner::~WindowRuntimeOwner() = default;
ClientRuntimeStorage& WindowRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
engine::UnloadState& WindowRuntimeOwner::unload_state() noexcept {
    return data_->state;
}
engine::UnloadRuntime& WindowRuntimeOwner::unload_runtime() noexcept {
    return data_->unload;
}
input::WindowSubclassRuntime& WindowRuntimeOwner::subclass() noexcept {
    return data_->subclass;
}
const engine::SourceInitializationImports& WindowRuntimeOwner::startup_imports() const noexcept {
    return data_->imports.startup;
}
void WindowRuntimeOwner::bind_subclass_callback() {
    input::bind_window_subclass_runtime(subclass());
}

StaticCrtImports native_static_crt_imports() {
    return {script::bind_loaded_panorama_v8_api().dispose_global};
}
namespace {
ClientRuntimeStorage& require_static_owners(CrtRuntimeOwner& crt, WindowRuntimeOwner& window,
                                            ui::RendererAssets& assets, StaticCrtImports imports) {
    auto& storage = crt.storage();
    if (&window.storage() != &storage || &assets.storage() != &storage ||
        (window.unload_state().event_listener &&
         window.unload_state().event_listener != &assets.device_listener()))
        throw std::invalid_argument(
            "Static CRT contexts require the canonical early renderer assets and window owners");
    if (!imports.dispose_v8_global)
        throw std::invalid_argument("Static CRT cleanup requires the real V8 DisposeGlobal import");
    return storage;
}
}
struct StaticCrtRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    StaticCrtInitialization static_initializers;
    engine::StartupVectorCleanupBindings native_vectors;
    engine::StartupContainerCleanup native_containers;
    Data(CrtRuntimeOwner& crt, WindowRuntimeOwner& window, ui::RendererAssets& assets,
         StaticCrtConstructorInputs words, StaticCrtImports imports)
        : storage(require_static_owners(crt, window, assets, imports)),
          static_initializers{storage.data_owner(), storage.exit_callbacks(), storage.memory(),
                              assets.icon_cache(),  words.flags_word,         words.keys_word},
          native_vectors(storage.data_owner(), storage.exit_callbacks(), storage.memory(),
                         imports.dispose_v8_global),
          native_containers({storage.exit_callbacks(), storage.memory(),
                             storage.data().native_cell<engine::StartupOwnedPointers>(0x43478DA8u),
                             storage.data().native_cell<engine::StartupOwnedTexts>(0x43479088u),
                             storage.data().native_cell<engine::StartupOwnedTexts>(0x43479078u),
                             storage.data().native_cell<engine::StartupOwnedModules>(0x434792E0u),
                             storage.data().native_cell<engine::StartupOwnedModules>(0x434792D0u),
                             storage.data().native_cell<engine::StartupTextBuckets>(0x4346D700u),
                             storage.data().native_cell<engine::StartupPointerBuckets>(0x4346DA40u),
                             assets.device_listener(), window.unload_state()}) {}
};
StaticCrtRuntimeOwner::StaticCrtRuntimeOwner(CrtRuntimeOwner& crt, WindowRuntimeOwner& window,
                                             ui::RendererAssets& assets,
                                             StaticCrtConstructorInputs words)
    : StaticCrtRuntimeOwner(crt, window, assets, words, native_static_crt_imports()) {}
StaticCrtRuntimeOwner::StaticCrtRuntimeOwner(CrtRuntimeOwner& crt, WindowRuntimeOwner& window,
                                             ui::RendererAssets& assets,
                                             StaticCrtConstructorInputs words,
                                             StaticCrtImports imports)
    : data_(std::make_unique<Data>(crt, window, assets, words, imports)) {}
StaticCrtRuntimeOwner::~StaticCrtRuntimeOwner() = default;
ClientRuntimeStorage& StaticCrtRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
StaticCrtInitialization& StaticCrtRuntimeOwner::static_initializers() noexcept {
    return data_->static_initializers;
}
engine::StartupVectorCleanupBindings& StaticCrtRuntimeOwner::native_vector_cleanup() noexcept {
    return data_->native_vectors;
}
engine::StartupContainerCleanup& StaticCrtRuntimeOwner::native_container_cleanup() noexcept {
    return data_->native_containers;
}
void StaticCrtRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument(
            "Static CRT callbacks must share the canonical source data owner");
    bind_static_crt_initializers(sources, static_initializers());
    engine::bind_startup_vector_cleanup(sources, native_vector_cleanup());
    engine::bind_startup_container_cleanup(sources, native_container_cleanup());
}
} //namespace skeet::client
