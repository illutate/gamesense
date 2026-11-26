#include "skeet/client/native_startup.hpp"
#include "skeet/client/runtime_owner_bindings.hpp"
#include "skeet/client/runtime_owner_initialization.hpp"
#include "skeet/client/static_crt_initialization.hpp"
#include "skeet/client/crt_initialization.hpp"
#include "skeet/events/event_name_storage.hpp"
#include "skeet/events/registry.hpp"
#include "skeet/engine/startup_vector_cleanup.hpp"
#include "skeet/engine/startup_container_cleanup.hpp"
#include "skeet/client/misc_runtime_factories.hpp"
#include "skeet/client/combat_runtime_factories.hpp"
#include "skeet/client/skin_runtime_factories.hpp"
#include "skeet/client/movement_hud_factories.hpp"
#include "skeet/client/history_runtime_factories.hpp"
#include "skeet/client/world_runtime_factories.hpp"
#include "skeet/client/game_window_proxy_factory.hpp"
#include "skeet/client/source_terminal_cleanup.hpp"
#include "skeet/rage/prediction_mask_owner.hpp"
#include "skeet/rage/post_firing.hpp"
#include "skeet/engine/map_game_system_owner.hpp"
#include "skeet/anti_aim/startup_initializers.hpp"
#include "skeet/client/exit_registration.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/client/static_cleanup.hpp"
#include "skeet/client/config_initializers.hpp"
#include "skeet/client/global_config_initializers.hpp"
#include "skeet/client/hook_descriptor_cleanup.hpp"
#include "skeet/client/prediction_history_lifecycle.hpp"
#include "skeet/client/entity_command_lifecycle.hpp"
#include "skeet/client/target_mask_lifecycle.hpp"
#include "skeet/client/shared_esp_owner.hpp"
#include "skeet/client/buffered_shot_lifecycle.hpp"
#include "skeet/effects/free_look_owner.hpp"
#include "skeet/engine/preserved_initialization.hpp"
#include "skeet/engine/first_startup_services.hpp"
#include "skeet/script/startup_initializers.hpp"
#include "skeet/script/ui_type_storage.hpp"
#include "skeet/animation/fix_animation_backup_cleanup.hpp"
#include "skeet/input/runtime.hpp"
#include "skeet/rage/group_storage.hpp"
#include "skeet/rage/feedback_owner.hpp"
#include "skeet/menu/settings_menu_storage.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/misc/viewmodel_lag_owner.hpp"
#include "skeet/hud/simple_owners.hpp"
#include "skeet/esp/flag_table_initializer.hpp"
#include <sstream>
#include <array>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::client {
namespace {
StartupExitProcess active_exit_process = nullptr;
std::string unmapped_message(std::uint32_t operand, std::uint32_t address, bool preflight) {
    std::ostringstream message;
    message << "Unfinished native startup " << (preflight ? "preflight" : "initializer")
            << " at operand0x" << std::hex << operand << ":0x" << address;
    return message.str();
}
void initialize_masks(void* graph) {
    auto& image = static_cast<NativeStartupSourceGraph*>(graph)->data;
    auto& matrix = image.native_cell<std::array<std::uint32_t, 32 * 33>>(0x43477be0);
    auto& prefixes = image.native_cell<std::array<std::uint32_t, 32>>(0x43477b60);
    initialize_native_startup_bit_masks({matrix, prefixes});
}
void initialize_menu_key(void* graph) {
    auto& source = *static_cast<NativeStartupSourceGraph*>(graph);
    input::initialize_menu_hotkey(source.configuration, source.menu_key);
}
void initialize_infinite_duck(void* graph) {
    auto& source = *static_cast<NativeStartupSourceGraph*>(graph);
    initialize_infinite_duck_config(source.configuration, source.movement.infinite_duck);
}
void initialize_air_duck(void* graph) {
    auto& source = *static_cast<NativeStartupSourceGraph*>(graph);
    initialize_air_duck_config(source.configuration, source.movement.air_duck);
}
void initialize_edge_jump(void* graph) {
    auto& source = *static_cast<NativeStartupSourceGraph*>(graph);
    initialize_edge_jump_hotkey(source.configuration, source.movement.edge_jump);
}
void initialize_skin_filter(void* graph) {
    auto& source = *static_cast<NativeStartupSourceGraph*>(graph);
    source.skins.initialize_shared(source.configuration);
}
void initialize_ping_hud(void*) {
    hud::initialize_ping_hud_owner();
}
void initialize_crosshair_hud(void*) {
    hud::initialize_crosshair_hud_owner();
}
void initialize_flags_container(void* raw) { //4332BB3A
    auto& graph = *static_cast<NativeStartupSourceGraph*>(raw);
    const auto& image = graph.data;
    image.native_cell<std::uint32_t>(0x4346d72cu) = graph.static_crt.flags_constructor_word;
    image.native_cell<void*>(0x4346d730u) = &image.native_cell<std::byte>(0x4346d734u);
    (void)graph.exits.callbacks.append(graph.native_vector_cleanup.source_finalizer(0x433c956cu));
}
void initialize_keys_container(void* raw) { //433412F8
    auto& graph = *static_cast<NativeStartupSourceGraph*>(raw);
    const auto& image = graph.data;
    image.native_cell<std::uint32_t>(0x4346daacu) = graph.static_crt.keys_constructor_word;
    image.native_cell<void*>(0x4346dab0u) = &image.native_cell<std::byte>(0x4346dab4u);
    (void)graph.exits.callbacks.append(graph.native_vector_cleanup.source_finalizer(0x433fbe15u));
}
void first_startup_services(void* context) {
    // Devirtualized from434822C8 ->434EA5D4; only source code is called.
    engine::initialize_first_startup_services(
        *static_cast<engine::FirstStartupServiceBindings*>(context));
}
void last_vm_callback(void* vm) {
    static_cast<engine::PreservedInitialization*>(vm)->last_startup_callback();
}
NativeStartupImports require_imports(NativeStartupImports imports) {
    if (!imports.clear_unhandled_exception_filter || !imports.terminate_thread ||
        !imports.exit_process)
        throw std::invalid_argument("Startup runtime requires its actual Windows imports");
    return imports;
}
void begin_startup(NativeStartupState state, NativeStartupImports imports,
                   std::uintptr_t argument) {
    active_exit_process = imports.exit_process;
    state.initialization_argument = argument;
    imports.clear_unhandled_exception_filter();
    state.fatal_handler = native_startup_fatal;
}
#if defined(_WIN32)
void clear_exception_filter() {
    (void)SetUnhandledExceptionFilter(nullptr);
}
#endif
}

void initialize_native_startup_bit_masks(NativeStartupBitMasks masks) noexcept { //433FB71E
    for (std::uint32_t row = 0; row < 32; ++row) {
        const auto prefix = (std::uint32_t(1) << row) - 1;
        for (std::uint32_t column = 0; column < 33; ++column) {
            const auto shift = row + column;
            masks.matrix[row * 33 + column] = shift < 32 ? (0xffffffffu << shift) | prefix : prefix;
        }
    }
    for (std::uint32_t row = 0; row < 32; ++row)
        masks.prefixes[row] = (std::uint32_t(1) << row) - 1;
}

UnmappedNativeInitializer::UnmappedNativeInitializer(std::uint32_t operand, std::uint32_t address,
                                                     bool is_preflight)
    : std::runtime_error(unmapped_message(operand, address, is_preflight)), operand(operand),
      native_address(address), preflight(is_preflight) {}

NativeStartupSources::NativeStartupSources(engine::NativeDataImage& data,
                                           NativeInitializerCall final_initializer,
                                           std::span<const NativeInitializerCall> initializers,
                                           std::span<const NativePreflightCall> preflight)
    : data_(data), final_initializer_(final_initializer) {
    if (final_initializer.native_address != 0x43484F06u || !final_initializer.owner ||
        !final_initializer.invoke)
        throw std::invalid_argument(
            "Startup requires the exact nonempty final initializer43484F06 binding");
    bind_initializer(final_initializer);
    for (const auto& source : initializers)
        bind_initializer(source);
    for (const auto& source : preflight)
        bind_preflight(source);
}
NativeStartupSources::NativeStartupSources(NativeStartupSourceGraph& graph,
                                           std::span<const NativeInitializerCall> initializers,
                                           std::span<const NativePreflightCall> preflight)
    : NativeStartupSources(graph.data, graph.final_initializer) {
    if (&graph.first_startup_services.data != &data_)
        throw std::invalid_argument("First startup must share the canonical service-number cells");
    if (&graph.configuration.descriptors != &data_.native_cell<ConfigLink>(0x434798bcu) ||
        &graph.configuration.hotkeys != &data_.native_cell<HotkeyLink>(0x434798c4u))
        throw std::invalid_argument(
            "Native startup requires the canonical configuration sentinels");
    if (&graph.menu_key != &data_.native_cell<RegisteredHotkey>(0x43478DB8u) ||
        &graph.global_configuration.settings_menu.menu_key != &graph.menu_key ||
        &graph.global_configuration.settings_menu.multithreading_config !=
            &data_.native_cell<BooleanConfig>(0x434683ECu) ||
        &graph.global_configuration.settings_menu.dpi_scale !=
            &data_.native_cell<std::int32_t>(0x43475A94u))
        throw std::invalid_argument(
            "CRT, menu and runtime settings must share their canonical source cells");
    if (graph.static_crt.memory.free != graph.runtime_owners.lifecycle.memory.free ||
        graph.static_crt.memory.process_heap !=
            graph.runtime_owners.lifecycle.memory.process_heap ||
        graph.event_names.memory.allocate != graph.runtime_owners.lifecycle.memory.allocate ||
        graph.event_names.memory.free != graph.runtime_owners.lifecycle.memory.free ||
        graph.event_names.memory.process_heap !=
            graph.runtime_owners.lifecycle.memory.process_heap ||
        graph.native_vector_cleanup.memory.free != graph.runtime_owners.lifecycle.memory.free ||
        graph.native_vector_cleanup.memory.process_heap !=
            graph.runtime_owners.lifecycle.memory.process_heap ||
        graph.native_container_cleanup.bindings().memory.free !=
            graph.runtime_owners.lifecycle.memory.free ||
        graph.native_container_cleanup.bindings().memory.process_heap !=
            graph.runtime_owners.lifecycle.memory.process_heap)
        throw std::invalid_argument(
            "CRT storage and runtime owners must share the canonical process heap");
    if (&graph.anti_aim.configuration != &graph.configuration ||
        &graph.static_configuration.configuration != &graph.configuration ||
        &graph.rage_groups.configuration != &graph.configuration ||
        &graph.prediction_history.configuration != &graph.configuration ||
        &graph.shared_esp.configuration != &graph.configuration ||
        &graph.shot_feedback.configuration != &graph.configuration ||
        &graph.global_configuration.configuration != &graph.configuration ||
        &graph.global_configuration.rage != &graph.static_configuration.rage ||
        &graph.global_configuration.other_esp != &graph.static_configuration.other_esp ||
        &graph.global_configuration.effects != &graph.static_configuration.effects ||
        &graph.global_configuration.settings_menu.menu_key != &graph.menu_key ||
        &graph.simple_hud.feature_indicators !=
            &graph.global_configuration.other_esp.feature_indicators ||
        &graph.simple_hud.overlays != &graph.global_configuration.other_esp.overlays ||
        &graph.global_configuration.free_look.key.value.binding !=
            &graph.free_look.bindings.view.free_look_key ||
        &graph.static_configuration.target_mask != &graph.target_mask.settings.enabled)
        throw std::invalid_argument("Native startup must share the configuration registries");
    if (&graph.hook_descriptors.callbacks != &graph.exits.callbacks ||
        &graph.script_extensions.exit_callbacks != &graph.exits.callbacks ||
        &graph.animation_backups.callbacks != &graph.exits.callbacks ||
        &graph.rage_groups.callbacks != &graph.exits.callbacks ||
        &graph.esp_flags.callbacks != &graph.exits.callbacks ||
        &graph.esp_flags.flags.image() != &data_ ||
        &graph.ui_types.callbacks != &graph.exits.callbacks || &graph.ui_types.image() != &data_ ||
        graph.static_crt.data.get() != &data_ ||
        &graph.static_crt.callbacks != &graph.exits.callbacks ||
        &graph.event_names.callbacks != &graph.exits.callbacks ||
        &graph.event_names.registry != &graph.runtime_owners.sequences.events ||
        &graph.event_names.registry.name_storage.image() != &data_ ||
        graph.native_vector_cleanup.data.get() != &data_ ||
        &graph.native_vector_cleanup.callbacks != &graph.exits.callbacks ||
        &graph.native_container_cleanup.exit_callbacks() != &graph.exits.callbacks ||
        &graph.rage_groups.groups.image() != &data_ || graph.entity_commands.data.get() != &data_ ||
        graph.target_mask.data.get() != &data_ ||
        &graph.entity_commands.registries.data_updates.image() != &data_ ||
        &graph.entity_commands.registries.recharge.image() != &data_ ||
        &graph.entity_commands.registries.prediction.image() != &data_ ||
        &graph.prediction_history.registries.data_updates.image() != &data_ ||
        &graph.prediction_history.registries.recharge.image() != &data_ ||
        &graph.prediction_history.registries.prediction.image() != &data_ ||
        &graph.prediction_history.registries.empty_ticks.image() != &data_ ||
        &graph.shared_esp.registries.empty_ticks.image() != &data_ ||
        &graph.shot_feedback.recharge.image() != &data_ ||
        &graph.exits.cleanup.owner_registry_storage().image() != &data_ ||
        &graph.hook_descriptors.storage.image() != &data_)
        throw std::invalid_argument(
            "Native startup must share its canonical data and exit registry");
    if (&graph.free_look.bindings.active !=
            &data_.native_cell<effects::FreeLookOwner*>(0x43475a64u) ||
        &graph.shared_esp.bindings.active !=
            &data_.native_cell<RejectedPlayerUpdateOwner*>(0x434758d0u) ||
        &graph.shot_feedback.active != &data_.native_cell<rage::FeedbackOwner*>(0x43475a7cu) ||
        &graph.simple_hud.entity_list != &data_.native_cell<void*>(0x4346a890u) ||
        &graph.simple_hud.cvar != &data_.native_cell<void*>(0x4346a87cu) ||
        &graph.simple_hud.suspended != &data_.native_cell<std::uint8_t>(0x43468e58u) ||
        &graph.simple_hud.ping_channel.bindings.history !=
            &data_.native_cell<misc::PingSpikeNetworkHistory>(0x4347a500u))
        throw std::invalid_argument("Startup factories require their canonical native owner slots");
    const auto& owner_storage = graph.exits.cleanup.owner_registry_storage();
    struct RegistryAlias {
        const engine::OwnerRegistry* registry;
        std::uint32_t address;
    };
    const std::array registry_aliases{
        RegistryAlias{&graph.prediction_history.registries.primary.registry, 0x43475b78u},
        RegistryAlias{&graph.entity_commands.registries.lifecycle.registry, 0x43475b78u},
        RegistryAlias{&graph.entity_commands.registries.map_entities.registry, 0x43475770u},
        RegistryAlias{&graph.free_look.lifecycle.registry, 0x43475b78u},
        RegistryAlias{&graph.free_look.updates.registry, 0x43475988u},
        RegistryAlias{&graph.target_mask.registries.primary.registry, 0x43475b78u},
        RegistryAlias{&graph.target_mask.registries.messages.registry, 0x43475b94u},
        RegistryAlias{&graph.target_mask.registries.hud.registry, 0x43475a9cu},
        RegistryAlias{&graph.shared_esp.registries.primary.registry, 0x43475b78u},
        RegistryAlias{&graph.shared_esp.registries.messages.registry, 0x43475b94u},
        RegistryAlias{&graph.shot_feedback.lifecycle.registry, 0x43475b78u},
        RegistryAlias{&graph.shot_feedback.packets.registry, 0x4347584cu},
        RegistryAlias{&graph.shot_feedback.frames.registry, 0x43475ad8u},
        RegistryAlias{&graph.shot_feedback.positions.registry, 0x43475930u},
        RegistryAlias{&graph.simple_hud.lifecycle.registry, 0x43475b78u},
        RegistryAlias{&graph.simple_hud.hud.registry, 0x43475a9cu}};
    for (const auto& alias : registry_aliases)
        if (alias.registry != &owner_storage.at(alias.address))
            throw std::invalid_argument(
                "Startup factories must share the native owner registry cells");
    const auto* const commands = &graph.entity_commands.registries.commands;
    if (&graph.prediction_history.registries.commands != commands ||
        &graph.target_mask.registries.commands != commands ||
        &graph.shared_esp.registries.commands != commands ||
        &graph.shot_feedback.commands != commands ||
        &graph.buffered_shots.lifecycle.commands != commands ||
        &graph.viewmodel_lag.registries.commands != commands)
        throw std::invalid_argument("Startup factories must share the command registry");
    hud::bind_simple_hud_owners_context(graph.simple_hud);
    if (&graph.runtime_owners.configuration != &graph.configuration ||
        &graph.runtime_owners.sequences.settings != &graph.skins ||
        &graph.runtime_owners.rage.command_owners != commands ||
        &graph.runtime_owners.entity_updates != &graph.entity_commands.registries.entities ||
        &graph.runtime_owners.legit.empty_tick_owners !=
            &graph.prediction_history.registries.empty_ticks ||
        &graph.runtime_owners.rage.commands.prediction_masks() != &graph.prediction_masks.active)
        throw std::invalid_argument(
            "Runtime factories must share startup configuration, registries and prediction masks");
    if (&graph.prediction_masks.active !=
            &data_.native_cell<rage::PredictionMaskOwner*>(0x43475a58u) ||
        &graph.prediction_masks.lifecycle.registry != &owner_storage.at(0x43475b78u) ||
        &graph.prediction_masks.frames.registry != &owner_storage.at(0x43475ad8u))
        throw std::invalid_argument(
            "Prediction masks require their canonical owner and frame registries");
    if (&graph.misc_owners.configuration != &graph.configuration ||
        &graph.combat_owners.configuration != &graph.configuration ||
        &graph.skin_owners.configuration != &graph.configuration ||
        &graph.movement_hud.configuration != &graph.configuration ||
        &graph.world_owners.configuration != &graph.configuration ||
        &graph.skin_owners.settings != &graph.skins || &graph.misc_owners.commands != commands ||
        &graph.skin_owners.commands != commands ||
        &graph.combat_owners.packet.commands != commands ||
        &graph.movement_hud.movement_registries.commands != commands ||
        &graph.misc_owners.entities != &graph.runtime_owners.entity_updates ||
        &graph.skin_owners.entity_updates != &graph.runtime_owners.entity_updates ||
        &graph.history_owners.entities != &graph.runtime_owners.entity_updates ||
        &graph.combat_owners.scripting.modules.ui_types != &graph.ui_types.storage ||
        &graph.world_owners.command_owners != commands)
        throw std::invalid_argument(
            "Runtime factory groups must share startup configuration and receivers");
    bind_initializer({0x434822c8, &graph.first_startup_services, first_startup_services});
    bind_initializer({0x433fb71e, &graph, initialize_masks});
    bind_initializer({0x434928a2, &graph, initialize_menu_key});
    bind_initializer({0x434933a6, &graph, initialize_infinite_duck});
    bind_initializer({0x43490b39, &graph, initialize_air_duck});
    bind_initializer({0x4349340b, &graph, initialize_edge_jump});
    bind_initializer({0x43492e21, &graph, initialize_skin_filter});
    anti_aim::bind_native_startup_initializers(*this, graph.anti_aim);
    bind_exit_initializers(*this, graph.exits);
    bind_native_config_initializers(*this, graph.static_configuration);
    bind_global_config_initializers(*this, graph.global_configuration);
    bind_hook_descriptor_initializers(*this, graph.hook_descriptors);
    script::bind_native_startup_initializers(*this, graph.script_extensions);
    animation::bind_fix_animation_backup_exit_initializer(*this, graph.animation_backups);
    rage::bind_native_group_initializers(*this, graph.rage_groups);
    bind_prediction_history_initializer(*this, graph.prediction_history);
    bind_native_entity_command_factory(*this, graph.entity_commands);
    effects::bind_free_look_initializer(*this, graph.free_look);
    bind_native_target_mask_factory(*this, graph.target_mask);
    bind_shared_esp_initializer(*this, graph.shared_esp);
    rage::bind_feedback_owner_factory(*this, graph.shot_feedback);
    bind_buffered_shot_factory(*this, graph.buffered_shots);
    misc::bind_viewmodel_lag_factory(*this, graph.viewmodel_lag);
    bind_initializer({0x4347dd87u, &graph.simple_hud, initialize_ping_hud});
    bind_initializer({0x43486bf7u, &graph.simple_hud, initialize_crosshair_hud});
    esp::bind_flag_table_initializer(*this, graph.esp_flags);
    script::bind_ui_type_initializer(*this, graph.ui_types);
    bind_static_crt_initializers(*this, graph.static_crt);
    events::bind_event_name_initializer(*this, graph.event_names);
    engine::bind_startup_vector_cleanup(*this, graph.native_vector_cleanup);
    engine::bind_startup_container_cleanup(*this, graph.native_container_cleanup);
    bind_initializer({0x4332bb3au, &graph, initialize_flags_container});
    bind_initializer({0x433412f8u, &graph, initialize_keys_container});
    bind_runtime_owner_factories(*this, graph.runtime_owners);
    rage::bind_prediction_mask_factory(*this, graph.prediction_masks);
    engine::bind_map_game_system_factory(*this, graph.map_game_system);
    bind_misc_runtime_factories(*this, graph.misc_owners);
    bind_combat_runtime_factories(*this, graph.combat_owners);
    bind_skin_runtime_factories(*this, graph.skin_owners);
    bind_movement_hud_factories(*this, graph.movement_hud);
    bind_history_runtime_factories(*this, graph.history_owners);
    bind_world_runtime_factories(*this, graph.world_owners);
    bind_game_window_proxy_factory(*this, graph.game_window_proxy);
    for (const auto& source : initializers)
        bind_initializer(source);
    for (const auto& source : preflight)
        bind_preflight(source);
    if (&graph.source_terminal_cleanup.callbacks != &graph.exits.callbacks ||
        &graph.source_terminal_cleanup.bridge != &graph.shot_feedback.state.scripts ||
        &graph.shot_feedback.state != &graph.history_owners.history.feedback ||
        &graph.shot_feedback.runtime.players != &graph.source_terminal_cleanup.players)
        throw std::invalid_argument(
            "Source terminal cleanup requires the graph's canonical process owners");
    graph.source_terminal_cleanup.require_producers(graph.combat_owners.scripting.runtime,
                                                    graph.combat_owners.scripting.modules,
                                                    graph.history_owners.history.recording);
    // Select this last so no native-header registration can replace actual
    // source Bridge/UI/history cleanup, including a caller's extra bindings.
    bind_source_terminal_cleanup(*this, graph.source_terminal_cleanup);
    // Validate both known source phases before any initializer body runs.
    // These fixed build configurations do not implement the protected accessor.
    preflight_crt_initialization(*this);
    preflight_runtime_owner_initialization(*this);
}
void NativeStartupSources::bind_initializer(NativeInitializerCall source) {
    // The caller supplies the actual final implementation once. Additional
    // binders cannot silently replace that required operation or its owner.
    if (source.native_address == 0x43484f06 &&
        (source.owner != final_initializer_.owner || source.invoke != final_initializer_.invoke))
        throw std::invalid_argument("The final startup initializer binding cannot be replaced");
    initializers_.insert_or_assign(source.native_address, source);
}
void NativeStartupSources::bind_preflight(NativePreflightCall source) {
    preflight_.insert_or_assign(source.native_address, source);
}
void NativeStartupSources::require_initializer(std::uint32_t operand, std::uint32_t address) const {
    const auto source = initializers_.find(address);
    if (source == initializers_.end() || !source->second.invoke)
        throw UnmappedNativeInitializer(operand, address, false);
}
void NativeStartupSources::require_preflight(std::uint32_t operand, std::uint32_t address) const {
    const auto source = preflight_.find(address);
    if (source == preflight_.end() || !source->second.invoke)
        throw UnmappedNativeInitializer(operand, address, true);
}
void NativeStartupSources::initialize(std::uint32_t operand, std::uint32_t address) const {
    const auto source = initializers_.find(address);
    if (source == initializers_.end() || !source->second.invoke)
        throw UnmappedNativeInitializer(operand, address, false);
    source->second.invoke(source->second.owner);
}
std::uint32_t NativeStartupSources::check(std::uint32_t operand, std::uint32_t address) const {
    const auto source = preflight_.find(address);
    if (source == preflight_.end() || !source->second.invoke)
        throw UnmappedNativeInitializer(operand, address, true);
    return source->second.invoke(source->second.owner);
}

NativeStartupImports native_startup_runtime_imports() {
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto terminate = GetProcAddress(native, "ZwTerminateThread");
    const auto exit = GetProcAddress(native, "RtlExitUserProcess");
    if (!terminate || !exit)
        throw std::runtime_error("Native startup service is not available");
    return {clear_exception_filter, reinterpret_cast<StartupTerminateThread>(terminate),
            reinterpret_cast<StartupExitProcess>(exit)};
#else
    throw std::runtime_error("Native startup services require Windows");
#endif
}

NativeStartupRuntime::NativeStartupRuntime(NativeStartupSources& sources,
                                           NativeStartupImports imports)
    : state_{sources.data().native_cell<std::uintptr_t>(0x43475AE8u),
             sources.data().native_cell<void (*)()>(0x43475BC4u)},
      sources_(sources), imports_(require_imports(imports)) {}

std::uint32_t NativeStartupRuntime::initialize(std::uintptr_t third_argument) {
    preflight_crt_initialization(sources_);
    begin_startup(state_, imports_, third_argument);
    const auto precheck = crt_initialization_precheck();
    if (precheck.identifier && sources_.check(precheck.operand, precheck.identifier))
        (void)imports_.terminate_thread(reinterpret_cast<void*>(~std::uintptr_t(1)), 5);
    for (const auto& slot : crt_initialization_plan())
        if (slot.identifier)
            sources_.initialize(slot.operand, slot.identifier);
    return 0;
}
void NativeStartupRuntime::fatal() {
    imports_.exit_process(0xFFFFFFF6u);
}

RetainedNativeStartupRuntime::RetainedNativeStartupRuntime(
    NativeStartupSources& sources, engine::PreservedInitialization& metadata,
    NativeStartupImports imports)
    : state_{sources.data().native_cell<std::uintptr_t>(0x43475AE8u),
             sources.data().native_cell<void (*)()>(0x43475BC4u)},
      sources_(sources), metadata_(metadata), imports_(require_imports(imports)) {
    if (&sources.data() != &metadata.image())
        throw std::invalid_argument(
            "Retained startup requires its executable environment's canonical data owner");
}
std::uint32_t RetainedNativeStartupRuntime::initialize(std::uintptr_t third_argument) {
    preflight_crt_initialization(sources_);
    begin_startup(state_, imports_, third_argument);
    for (std::uint32_t operand = 0x2dc; operand != 0x2e0; operand += 4) {
        const auto native_address = static_cast<std::uint32_t>(metadata_.operand(operand));
        if (native_address && sources_.check(operand, native_address)) {
            (void)imports_.terminate_thread(reinterpret_cast<void*>(~std::uintptr_t(1)), 5);
            break;
        }
    }
    for (std::uint32_t operand = 0; operand != 0x2d8; operand += 4) {
        const auto native_address = static_cast<std::uint32_t>(metadata_.operand(operand));
        if (native_address)
            sources_.initialize(operand, native_address);
    }
    // Native writesEAX0. Its trailing43481E98 INT1 return-transfer ABI remains
    // outside this source entry; no raw ABI thunk is invented here.
    return 0;
}
void RetainedNativeStartupRuntime::fatal() {
    imports_.exit_process(0xFFFFFFF6u);
}
NativeInitializerCall
retained_final_startup_initializer(engine::PreservedInitialization& metadata) {
    return {0x43484F06u, &metadata, last_vm_callback};
}
void native_startup_fatal() {
    if (!active_exit_process)
        throw std::logic_error("Startup fatal import is not bound");
    active_exit_process(0xFFFFFFF6u);
}
}
