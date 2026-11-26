#include "skeet/client/movement_hud_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include "skeet/movement/command_bindings.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
std::uint32_t field(ClientRuntimeStorage& storage, std::uint32_t identity) {
    const auto& globals = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                globals, identity, row.xor_key_identity, row.initial_xor_key, 0)));
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                globals, identity, row.xor_key_identity, row.initial_xor_key, 0)));
    throw std::invalid_argument("Movement/HUD field has no proven netvar or datamap recipe");
}
std::uint32_t derived(ClientRuntimeStorage& storage, std::uint32_t pointer, std::uint32_t key,
                      std::uint32_t salt) {
    return std::uint32_t(
        std::ptrdiff_t(engine::DecodedOffset(storage.offsets().globals, pointer, key, salt, 0)));
}
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t identity) {
    auto* pointer = data.native_cell<T*>(identity);
    if (!pointer)
        throw std::invalid_argument("Movement/HUD requires an initialized engine alias");
    return *pointer;
}
engine::NativeConVarReference variable(const EngineRuntimeBindings& engine,
                                       std::uint32_t identity) {
    const auto result = engine.variables.at(identity);
    if (!result.get())
        throw std::invalid_argument("Movement/HUD requires its acquired engine ConVar");
    return result;
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage, std::uint32_t identity) {
    return {storage.owner_registries().at(identity), storage.memory()};
}
const std::uint32_t& bits(const std::int32_t& value) noexcept {
    // Corresponding signed/unsigned integer types may alias the same object.
    return reinterpret_cast<const std::uint32_t&>(value);
}
MovementEnvironmentFields environment_fields(ClientRuntimeStorage& s) {
    return {field(s, 0x434685CCu), field(s, 0x4346A398u), field(s, 0x4346A7E0u),
            field(s, 0x43467A74u), field(s, 0x434688D0u), field(s, 0x434679F8u),
            field(s, 0x43467DD4u)};
}
MovementCaptureOffsets capture_fields(ClientRuntimeStorage& s) {
    return {field(s, 0x4346791Cu), field(s, 0x43467DD4u), field(s, 0x434679F8u),
            field(s, 0x43468A34u), field(s, 0x43468B30u), field(s, 0x4346A398u),
            field(s, 0x4346A464u), field(s, 0x43469374u), field(s, 0x43468BD4u)};
}
MovementCommandFields command_fields(ClientRuntimeStorage& s) {
    return {field(s, 0x4346A8E4u), field(s, 0x43468BD4u),
            field(s, 0x434678A4u), field(s, 0x43468B30u),
            field(s, 0x43468558u), field(s, 0x43467200u),
            field(s, 0x4346A80Cu), field(s, 0x43467A50u),
            field(s, 0x434684CCu), derived(s, 0x43467430u, 0x4346A718u, 0xB8969256u),
            field(s, 0x4346908Cu), field(s, 0x434693D8u),
            field(s, 0x434670CCu), field(s, 0x4346A6DCu)};
}
BlockbotOffsets blockbot_fields(ClientRuntimeStorage& s) {
    return {field(s, 0x43468BD4u), field(s, 0x434688D0u),
            field(s, 0x434688B0u), field(s, 0x43468A34u),
            field(s, 0x43468434u), field(s, 0x434670CCu),
            field(s, 0x434685CCu), field(s, 0x43468E78u),
            field(s, 0x4346A5DCu), derived(s, 0x43468BF8u, 0x43467B8Cu, 0xD070684Cu),
            field(s, 0x43467910u)};
}
hud::ScreenOverlayFields overlay_fields(ClientRuntimeStorage& s) {
    return {field(s, 0x4346908Cu), field(s, 0x43467B44u), field(s, 0x434682B8u),
            field(s, 0x43467F74u), field(s, 0x43467A50u), field(s, 0x43467200u),
            field(s, 0x4346A80Cu), field(s, 0x4346A620u)};
}
ClientRuntimeStorage& require_shared(ClientRuntimeStorage& s, const EngineRuntimeBindings& engine,
                                     ConfigurationRuntimeOwner& configuration,
                                     engine::SceneQueryRuntimeOwner& scene,
                                     const StandaloneMovementHudViews& views,
                                     const MovementHudRuntimeImports& imports) {
    auto& data = s.data();
    auto* globals = data.native_cell<std::byte*>(0x43467B04u);
    auto* client_state = data.native_cell<std::byte*>(0x43468E44u);
    if (&configuration.storage() != &s || &scene.storage() != &s ||
        &scene.players().storage() != &s || &engine.variables.data() != &data ||
        &views.scripts.name_storage != &s.event_names() ||
        &views.budget.game_rules != &data.native_cell<void**>(0x4346A3A4u) ||
        &views.budget.configured != &data.native_cell<std::int32_t>(0x43469274u))
        throw std::invalid_argument(
            "Movement/HUD requires the same configuration, scene, script and command owners");
    const auto core = engine.interfaces.core();
    if (!globals || !client_state || !core.engine_client || !core.entity_list || !core.cvar ||
        core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        core.cvar != data.native_cell<void*>(0x4346A87Cu) ||
        !data.native_cell<void*>(0x4346A894u) || !data.native_cell<void*>(0x4346A89Cu) ||
        !data.native_cell<void**>(0x4346A3A4u) ||
        reinterpret_cast<std::byte*>(data.native_cell<float*>(0x4346A338u)) != globals + 0x20 ||
        reinterpret_cast<std::byte*>(data.native_cell<float*>(0x43467EB8u)) != globals + 0x10 ||
        reinterpret_cast<std::byte*>(data.native_cell<float*>(0x4346A334u)) != globals ||
        reinterpret_cast<std::byte*>(data.native_cell<std::int32_t*>(0x43469300u)) !=
            globals + 0x18 ||
        reinterpret_cast<std::byte*>(data.native_cell<std::int32_t*>(0x434757DCu)) !=
            client_state + 0x4D28 ||
        reinterpret_cast<std::byte*>(data.native_cell<std::int32_t*>(0x43475B2Cu)) !=
            client_state + 0x4D24)
        throw std::invalid_argument(
            "Movement/HUD requires canonical engine objects and live clock/client-state aliases");
    if (!imports.trigonometry.sine || !imports.trigonometry.cosine || !imports.rotation.sine ||
        !imports.rotation.cosine || !imports.rotation.arc_cosine ||
        !imports.rotation.arc_tangent2 || !imports.exponential || !imports.tangent)
        throw std::invalid_argument("Movement/HUD requires genuine scalar math boundaries");
    // Native target adjustment state belongs to the one PlayerRuntimeOwner.
    // TargetOffsets are the history owner's already proven fields, not names
    // guessed from a second independent engine-property lookup.
    if (std::uint32_t(views.target_offsets.team) != field(s, 0x434688B0u) ||
        std::uint32_t(views.target_offsets.origin) != field(s, 0x434670CCu) ||
        std::uint32_t(views.target_offsets.weapon_identifier) != field(s, 0x4346908Cu))
        throw std::invalid_argument(
            "Movement/HUD target fields differ from canonical offset storage");
    return s;
}
StandaloneMovementHudViews
canonical_views(ClientRuntimeStorage& storage, engine::SceneQueryRuntimeOwner& scene,
                VisualRuntimeOwner& visual, script::SourceScriptRuntimeOwner& scripts,
                CommandCaptureRuntimeOwner& capture, HistoryRuntimeOwner& history) {
    if (&visual.storage() != &storage || &scripts.storage() != &storage ||
        &capture.storage() != &storage || &history.storage() != &storage ||
        &history.players() != &scene.players())
        throw std::invalid_argument(
            "Movement/HUD production views must belong to the same process owners");
    return {scripts.bridge(),    capture.budget(),    history.target_offsets(),
            visual.projection(), visual.indicators(), visual.screen_size()};
}
}
MovementHudRuntimeImports native_movement_hud_runtime_imports() {
    const auto& trig = animation::native_sse_trigonometry();
    const auto numeric = lagcomp::native_history_numeric_bindings();
    return {trig,
            {trig.sine, trig.cosine, numeric.arccosine, animation::native_sse_atan2},
            animation::native_sse_exponential,
            animation::native_sse_tangent};
}

struct MovementHudRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    ConfigurationRuntimeOwner& configuration;
    const StandaloneMovementHudViews views;
    const MovementHudRuntimeImports imports;
    const std::uint32_t life;
    MovementCommandOwner state;
    MovementCaptureOffsets capture_offsets;
    MovementCaptureConvars capture_variables;
    MovementEnvironmentBindings environment;
    MovementCaptureInput capture;
    MovementExtrapolationLiveBindings live;
    MovementCommandBindings command_bindings;
    MovementCommandPhaseBindings phase_bindings;
    MovementCommandPhaseOwner command;
    MovementReleaseRuntime release;
    MovementOwnerHudBindings movement_hud;
    BlockbotConfig blockbot_settings;
    BlockbotState blockbot_state;
    BlockbotBindings blockbot;
    hud::ScreenOverlayState overlay_state;
    hud::ScreenOverlayPublisher overlay_publisher;
    hud::ScreenOverlayFields overlay_offsets;
    hud::ScreenOverlayUpdateBindings overlay_bindings;
    ScreenOverlayCommandOwner overlay_command;
    hud::ScreenOverlayHudBinding overlay_render;
    rage::TargetBindings targeting;
    hud::PenetrationReticleState reticle_state;
    hud::PenetrationReticleEnvironment reticle_environment;
    PenetrationReticleCommandOwner reticle_command;
    hud::PenetrationReticleBindings reticle_fields;
    hud::PenetrationHudBinding reticle_render;
    MovementHudFactoryBindings factories;

    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, engine::SceneQueryRuntimeOwner& scene,
         StandaloneMovementHudViews dependencies, MovementHudRuntimeImports external)
        : storage(require_shared(source, engine, config, scene, dependencies, external)),
          configuration(config), views(dependencies), imports(external),
          life(field(source, 0x43468E78u)), capture_offsets(capture_fields(source)),
          capture_variables{variable(engine, 0x43467A54u), variable(engine, 0x43467990u),
                            variable(engine, 0x43467638u), variable(engine, 0x43467E4Cu),
                            variable(engine, 0x43468F4Cu), variable(engine, 0x43468FB4u),
                            variable(engine, 0x4346A88Cu), variable(engine, 0x43467124u),
                            variable(engine, 0x4346A820u), variable(engine, 0x43467968u),
                            variable(engine, 0x434674E4u)},
          environment{environment_fields(source),
                      {variable(engine, 0x43467E28u), variable(engine, 0x43467FA0u),
                       variable(engine, 0x43468F4Cu), variable(engine, 0x43467124u),
                       variable(engine, 0x4346A820u), variable(engine, 0x43467968u)},
                      source.data().native_cell<void*>(0x4346A890u),
                      &pointed<float>(source.data(), 0x4346A338u),
                      source.data().native_cell<std::uint8_t*>(0x434683D0u)},
          capture{capture_offsets, capture_variables, pointed<float>(source.data(), 0x4346A338u),
                  &environment},
          live{capture, environment},
          command_bindings{environment,
                           capture,
                           scene.queries().movement(),
                           scene.queries().ballistics(),
                           source.data().native_cell<void*>(0x4346A514u),
                           source.data().native_cell<void*>(0x43469268u),
                           source.input_runtime().bitmap(),
                           command_fields(source),
                           views.budget,
                           config.rage_settings().global.duck_peek_assist.value.binding,
                           config.movement().infinite_duck.value,
                           config.anti_aim_other().slow_motion_key.value.binding,
                           config.rage_settings().global.activation.value.binding,
                           BunnyHopGuard(engine.variables.at(0x4346A4E4u)),
                           variable(engine, 0x43467D7Cu),
                           engine.variables.at(0x43468EA4u),
                           pointed<float>(source.data(), 0x43467EB8u),
                           pointed<float>(source.data(), 0x4346A334u),
                           pointed<std::int32_t>(source.data(), 0x43475B2Cu),
                           source.data().native_cell<std::int32_t>(0x4346DB1Cu),
                           source.data().native_cell<std::int32_t>(0x4346DB2Cu),
                           source.data().native_cell<std::int32_t>(0x43468488u),
                           bits(config.rage_settings().global.quick_stop_options.value),
                           config.anti_aim_global().enabled.value,
                           config.anti_aim_global().pitch.value,
                           config.anti_aim_global().body_yaw.value,
                           source.data().native_cell<EntityCommandOwner*>(0x43475898u),
                           source.data().native_cell<std::array<std::uint32_t, 150>>(0x43476090u),
                           source.data().native_cell<std::uint32_t>(0x434759C0u)},
          phase_bindings{command_bindings, source.data().native_cell<std::int32_t*>(0x434757DCu),
                         config.movement().edge_jump.value.binding, field(source, 0x43468B50u)},
          command{state, phase_bindings},
          release{
              state.config,
              {state.limits.angle_scale, state.side_magnitude, state.limits.maximum_angle,
               state.brake_threshold, state.angle_step},
              source.input_runtime().bitmap(),
              variable(engine, 0x4346A4E4u),
              variable(engine, 0x43468EA4u),
              state.jump_speed_cap,
              state.air_accelerate,
              config.movement().air_duck.value,
              pointed<float>(source.data(), 0x4346A334u),
              source.data().native_cell<void*>(0x4346A514u),
              {field(source, 0x434678A4u), field(source, 0x4346908Cu), field(source, 0x434670CCu)},
              environment,
              capture,
              scene.queries().movement(),
              scene.queries().ballistics(),
              source.data().native_cell<void*>(0x43469268u)},
          movement_hud{views.scripts,
                       config.other_esp_settings().feature_indicators,
                       config.movement().infinite_duck.value,
                       config.rage_settings().global,
                       source.data().native_cell<std::uint32_t>(0x434759C0u),
                       views.indicators,
                       views.projection,
                       life},
          blockbot{source.data().native_cell<void*>(0x4346A514u),
                   source.data().native_cell<void*>(0x4346A890u),
                   source.data().native_cell<void**>(0x4346A3A4u),
                   &pointed<std::int32_t>(source.data(), 0x43469300u),
                   &source.data().native_cell<lagcomp::TickTimingSamples>(0x43478C70u),
                   &pointed<float>(source.data(), 0x4346A338u),
                   &scene.queries().movement(),
                   source.data().native_cell<std::uint8_t*>(0x434683D0u),
                   blockbot_fields(source)},
          overlay_publisher(source.data().native_cell<bool>(0x43475894u),
                            source.data().native_cell<void*>(0x43475A24u)),
          overlay_offsets(overlay_fields(source)),
          overlay_bindings{source.data().native_cell<void*>(0x4346A514u),
                           source.data().native_cell<void*>(0x4346A894u),
                           source.data().native_cell<void*>(0x4346A87Cu),
                           &pointed<float>(source.data(), 0x43467EB8u),
                           &pointed<float>(source.data(), 0x4346A338u),
                           &pointed<float>(source.data(), 0x4346A338u),
                           &source.data().native_cell<float>(0x43468E80u),
                           variable(engine, 0x4346794Cu),
                           variable(engine, 0x43467104u),
                           variable(engine, 0x43468D8Cu),
                           variable(engine, 0x43467BB4u),
                           variable(engine, 0x4346A480u),
                           source.data().native_cell<EntityCommandOwner*>(0x43475898u),
                           imports.rotation,
                           imports.exponential,
                           imports.tangent},
          overlay_command{overlay_state, overlay_publisher, config.other_esp_settings().overlays,
                          overlay_offsets, overlay_bindings},
          overlay_render{config.other_esp_settings().overlays,
                         overlay_state,
                         life,
                         views.screen_size,
                         views.projection,
                         source.data().native_cell<RageVector>(0x43479CCCu),
                         imports.trigonometry},
          targeting{source.data().native_cell<void*>(0x43475808u),
                    source.data().native_cell<void*>(0x4346A890u),
                    variable(engine, 0x43467A8Cu),
                    variable(engine, 0x4346A568u),
                    scene.players().adjustments(),
                    views.target_offsets},
          reticle_environment{targeting,
                              &pointed<std::int32_t>(source.data(), 0x43469300u),
                              scene.queries().ballistics(),
                              field(source, 0x4346A808u),
                              source.data().native_cell<const std::int32_t*>(0x43469060u),
                              field(source, 0x434683C8u),
                              std::ptrdiff_t(field(source, 0x43469344u)) - 0x18,
                              source.data().native_cell<void*>(0x43468304u),
                              source.data().native_cell<void* const*>(0x43475B90u)},
          reticle_command{reticle_state, config.other_esp_settings().overlays, reticle_environment},
          reticle_fields{source.data().native_cell<void*>(0x4346A890u), life,
                         field(source, 0x43467A74u), field(source, 0x4346908Cu)},
          reticle_render{config.other_esp_settings().overlays, reticle_state, reticle_fields,
                         views.screen_size},
          factories{source.configuration(),
                    {registry(source, 0x43475B78u), source.command_phases(),
                     registry(source, 0x43475A9Cu)},
                    {command, movement_hud, source.data().native_cell<void*>(0x4346A514u),
                     source.data().native_cell<void*>(0x4346A89Cu),
                     engine.variables.at(0x434674E4u), engine.variables.at(0x43467FA0u),
                     source.data().native_cell<MovementCommandOwner*>(0x434759B0u)},
                    blockbot_settings,
                    blockbot_state,
                    blockbot,
                    views.projection,
                    life,
                    {registry(source, 0x43475B78u), source.command_phases(),
                     registry(source, 0x43475A9Cu)},
                    overlay_command,
                    overlay_render,
                    {registry(source, 0x43475B78u), source.command_phases(),
                     registry(source, 0x43475A9Cu)},
                    reticle_command,
                    reticle_environment,
                    source.data().native_cell<void*>(0x43468304u),
                    reticle_render,
                    config.other_esp_settings().overlays} {}
};
MovementHudRuntimeOwner::MovementHudRuntimeOwner(
    ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
    ConfigurationRuntimeOwner& configuration, engine::SceneQueryRuntimeOwner& scene,
    VisualRuntimeOwner& visual, script::SourceScriptRuntimeOwner& scripts,
    CommandCaptureRuntimeOwner& capture, HistoryRuntimeOwner& history)
    : MovementHudRuntimeOwner(storage, engine, configuration, scene,
                              canonical_views(storage, scene, visual, scripts, capture, history),
                              native_movement_hud_runtime_imports()) {}
MovementHudRuntimeOwner::MovementHudRuntimeOwner(ClientRuntimeStorage& storage,
                                                 const EngineRuntimeBindings& engine,
                                                 ConfigurationRuntimeOwner& configuration,
                                                 engine::SceneQueryRuntimeOwner& scene,
                                                 StandaloneMovementHudViews views,
                                                 MovementHudRuntimeImports imports)
    : data_(std::make_unique<Data>(storage, engine, configuration, scene, views, imports)) {}
MovementHudRuntimeOwner::~MovementHudRuntimeOwner() = default;
ClientRuntimeStorage& MovementHudRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
const MovementEnvironmentBindings& MovementHudRuntimeOwner::movement_environment() const noexcept {
    return data_->environment;
}
const MovementCaptureInput& MovementHudRuntimeOwner::movement_capture() const noexcept {
    return data_->capture;
}
const MovementExtrapolationLiveBindings& MovementHudRuntimeOwner::live_movement() const noexcept {
    return data_->live;
}
MovementCommandPhaseOwner& MovementHudRuntimeOwner::movement_command() noexcept {
    return data_->command;
}
MovementReleaseRuntime& MovementHudRuntimeOwner::release_movement() noexcept {
    return data_->release;
}
MovementHudFactoryBindings& MovementHudRuntimeOwner::factory_bindings() noexcept {
    return data_->factories;
}
void MovementHudRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument(
            "Movement/HUD factories require the canonical source scheduler");
    bind_movement_hud_factories(sources, data_->factories);
}
} //namespace skeet::client
