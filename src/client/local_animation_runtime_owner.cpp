#include "skeet/client/local_animation_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/player_command_queries.hpp"
#include "skeet/client/player_physics_hooks.hpp"
#include "skeet/client/player_command_hooks.hpp"
#include "skeet/client/local_player_animation_hooks.hpp"
#include "skeet/effects/runtime_owner.hpp"
#include "skeet/engine/build_local_animation_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include "skeet/prediction/player_simulation.hpp"
#include "skeet/movement/global_config.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/anti_aim/config.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
struct Derived {
    std::uint32_t pointer, key, salt;
};
constexpr std::array derived{
    Derived{0x43468FD0u, 0x43467C70u, 0xF58E6118u}, Derived{0x43467F4Cu, 0x4346A8B0u, 0x3D52A1A7u},
    Derived{0x43468368u, 0x43467FACu, 0x3B1F6ECCu}, Derived{0x43468D30u, 0x43468ED4u, 0x2A671B42u},
    Derived{0x43468E7Cu, 0x434674A8u, 0x54783141u}, Derived{0x43467634u, 0x43468F2Cu, 0xBB78DC43u},
    Derived{0x43468BF8u, 0x43467B8Cu, 0xD070684Cu}};
std::ptrdiff_t field(ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& globals = storage.offsets().globals;
    // XOR salts are not field identities: CTEFireBullets::m_vecOrigin also
    // uses 2A671B42. These player fields must retain their exact publications.
    for (const auto& row : derived)
        if (row.salt == salt)
            return engine::DecodedOffset(globals, row.pointer, row.key, salt, 0);
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.initial_xor_key == salt)
            return engine::DecodedOffset(globals, row.pointer_identity, row.xor_key_identity, salt,
                                         0);
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.initial_xor_key == salt)
            return engine::DecodedOffset(globals, row.pointer_identity, row.xor_key_identity, salt,
                                         0);
    throw std::invalid_argument("Local animation field lacks a recovered canonical offset recipe");
}
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t identity) {
    auto* const value = data.native_cell<T*>(identity);
    if (!value)
        throw std::invalid_argument("Local animation requires its published engine clock alias");
    return *value;
}
RestrictedPlayerCommandOffsets restricted_fields(ClientRuntimeStorage& storage,
                                                 const lagcomp::EntityNetvars& common) {
    const auto f = [&](std::uint32_t salt) { return field(storage, salt); };
    return {common,        f(0x66146802), f(0x00A5BD68), f(0x52899DF3), f(0x1BAE1168),
            f(0xF58E6118), f(0xCD812654), f(0x3D10B94C), f(0x36375C23), f(0x2A671B42),
            f(0x3309C0CF), f(0xE2FBE695), f(0xAC7F2913), f(0x7090D8DD), f(0xCACAEECC),
            f(0xA15C3105), f(0xAE838771), f(0xD4248210), f(0xA3ABBB09), f(0x7501CB91),
            f(0x7A2D60C1), f(0x4E66BB34), f(0xA9EE6062), f(0xF8205EBA)};
}
LocalPlayerAnimationHookOffsets local_fields(ClientRuntimeStorage& storage) {
    const auto f = [&](std::uint32_t salt) { return field(storage, salt); };
    return {f(0xF58E6118), f(0x3D52A1A7), f(0x00A5BD68), f(0x9D920E35), f(0x2F4E97C4),
            f(0xC246315E), f(0xA25D8271), f(0x3B1F6ECC), f(0x8B5586F6), f(0xA15C3105),
            f(0x54783141), f(0x2B554AD0), f(0xC7E1ECD2), f(0x0471E13C), f(0xBB78DC43)};
}
animation::LocalAnimationOffsets helper_fields(ClientRuntimeStorage& storage) {
    const auto f = [&](std::uint32_t salt) { return field(storage, salt); };
    return {f(0xB538BBA5), f(0x32E4DAF1), f(0x3D52A1A7), f(0xCD812654),
            f(0x5C9F0FBB), f(0xE2FBE695), f(0xAC7F2913), f(0x52899DF3)};
}
prediction::PlayerSimulationOffsets simulation_fields(ClientRuntimeStorage& storage) {
    const auto f = [&](std::uint32_t salt) { return field(storage, salt); };
    return {f(0x5DF1A4BC), f(0xD070684C), f(0x9D920E35), f(0xA249061A),
            f(0x3D10B94C), f(0x53E7C7D2), f(0x1BAE1168), f(0x66146802)};
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history,
                              CommandCaptureRuntimeOwner& capture,
                              effects::EffectsRuntimeOwner& effects) {
    auto& data = storage.data();
    auto* const clock = data.native_cell<std::byte*>(0x43467B04u);
    const auto& entities = history.entity_runtime();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &history.storage() != &storage || &capture.storage() != &storage ||
        &effects.storage() != &storage || &effects.view().settings != &config.effects_settings() ||
        entities.entity_list != data.native_cell<void*>(0x4346A890u) ||
        &entities.interval != &pointed<float>(data, 0x4346A338u) ||
        &entities.frame != &pointed<std::int32_t>(data, 0x434678F8u))
        throw std::invalid_argument(
            "Local animation requires the same canonical feature, history and capture owners");
    if (!clock || data.native_cell<void*>(0x43467EB8u) != clock + 0x10 ||
        data.native_cell<void*>(0x4346A338u) != clock + 0x20 ||
        data.native_cell<void*>(0x434678F8u) != clock + 4 ||
        !data.native_cell<void*>(0x43475B2Cu) || !data.native_cell<void*>(0x43468D4Cu) ||
        !data.native_cell<void*>(0x4346A894u) || !data.native_cell<void*>(0x434693F0u) ||
        !data.native_cell<void*>(0x4346A364u) || !data.native_cell<void*>(0x434678C8u) ||
        !engine.command.random_seed || !engine.command.prediction_player ||
        engine.command.random_seed != data.native_cell<std::int32_t*>(0x43468328u) ||
        engine.command.prediction_player != data.native_cell<void**>(0x434693A4u))
        throw std::invalid_argument(
            "Local animation requires genuine command, clock and history engine producers");
    for (const auto identity : {0x4346794Cu, 0x43467AA0u, 0x43468C2Cu, 0x4346A648u})
        if (!engine.variables.at(identity).get())
            throw std::invalid_argument("Local animation requires its actual acquired ConVars");
    return storage;
}
}
struct LocalAnimationRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    HistoryRuntimeOwner& history;
    effects::EffectsRuntimeOwner& effects;
    engine::BuildLocalAnimationBindings engine_entries;
    animation::AnimationAngleHistoryView angles;
    animation::AnimationUpdateEngine update_engine;
    animation::LocalAnimationBindings helpers;
    effects::ViewDuckBindings duck;
    misc::NetworkFixProxyRuntime proxy;
    misc::NetworkFixBindings network_fix;
    hud::CrosshairViewBindings crosshair;
    LocalPlayerAnimationHookBindings local_bindings;
    LocalPlayerAnimationHooks local;
    PlayerCommandQueryBindings query_bindings;
    PlayerCommandQueries queries;
    PlayerPhysicsHooks physics;
    PlayerCommandHookBindings command_bindings;
    PlayerCommandHooks command_hooks;
    prediction::PlayerSimulationBindings simulation;
    RestrictedPlayerCommandBindings restricted;
    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history_owner,
         CommandCaptureRuntimeOwner& capture, effects::EffectsRuntimeOwner& effect_owner)
        : storage(checked(source, engine, config, history_owner, capture, effect_owner)),
          history(history_owner), effects(effect_owner),
          engine_entries(engine::initialize_build_local_animation_bindings(source.data())),
          angles(source.data().native_cell<std::uint8_t>(0x43468D7Cu),
                 source.data().native_cell<const std::int32_t*>(0x43475B2Cu),
                 source.data().native_cell<std::array<animation::AnimationAngleSample, 2>>(
                     0x434759DCu)),
          update_engine(lagcomp::native_history_animation_engine(
              source.data().native_cell<void*>(0x434693F0u))),
          helpers{helper_fields(source),
                  source.data().native_cell<void*>(0x4346A890u),
                  engine_entries.symbol_table,
                  engine_entries.initialize_mapping,
                  engine_entries.select_symbol_sequence,
                  engine_entries.find_symbol,
                  engine_entries.add_symbol,
                  engine_entries.activity_vtable,
                  engine.command.random_seed,
                  engine.command.prediction_player,
                  source.data().native_cell<void*>(0x43475808u),
                  source.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                  source.data().native_cell<engine::MemberFunction<std::int32_t, std::int32_t>>(
                      0x4346A364u),
                  engine_entries.layer_weight,
                  engine::native_local_layer_weight_engine(engine_entries.layer_weight),
                  source.data().native_cell<engine::MemberFunction<const char*, std::int32_t>>(
                      0x434678C8u),
                  {}},
          duck{effects::ViewDuckTarget(
                   source.data().native_cell<MovementCommandOwner*>(0x434759B0u)),
               config.movement().infinite_duck.value,
               source.data().native_cell<std::uint32_t>(0x434759C0u),
               config.rage_settings().global.duck_peek_assist.value.binding,
               source.input_runtime().bitmap(),
               source.data().native_cell<float>(0x4346D654u),
               std::uint32_t(field(source, 0x3309C0CF)),
               std::uint32_t(field(source, 0xC246315E))},
          proxy({source.data().native_cell<misc::NetworkFixEngineEntry>(0x4346A454u),
                 source.data().native_cell<std::uint32_t>(0x4346A648u),
                 source.data().native_cell<std::uintptr_t>(0x4346A64Cu)}),
          network_fix{config.settings_menu().fix_networking,
                      source.data().native_cell<std::uintptr_t>(0x434689A0u),
                      source.data().native_cell<void*>(0x4346A64Cu),
                      source.data().native_cell<std::uintptr_t>(0x434692F4u),
                      source.data().native_cell<void*>(0x4346A514u),
                      source.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                      {field(source, 0x9D920E35), field(source, 0x7E9BA7BC),
                       field(source, 0xAC7F2913), field(source, 0xF58E6118)},
                      proxy},
          crosshair{
              source.data().native_cell<void*>(0x4346A890u),
              {std::uint32_t(field(source, 0x9D920E35)), std::uint32_t(field(source, 0xCACAEECC)),
               std::uint32_t(field(source, 0xCD812654)), std::uint32_t(field(source, 0xA1AF8FFC))}},
          local_bindings{local_fields(source),
                         source.data().native_cell<std::uint8_t>(0x43475BD3u),
                         source.data().native_cell<std::uintptr_t>(0x434688C4u),
                         source.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                         config.effects_settings().third_person_alive.value,
                         source.data().native_cell<void*>(0x4346A894u),
                         engine.variables.at(0x43467AA0u),
                         source.data().native_cell<void*>(0x43468304u),
                         source.data().native_cell<std::uint8_t*>(0x434683D0u),
                         effect_owner.view(),
                         duck,
                         config.rage_settings().global.enabled.value,
                         config.anti_aim_global().enabled.value,
                         pointed<std::int32_t>(source.data(), 0x434678F8u),
                         pointed<float>(source.data(), 0x43467EB8u),
                         source.data().native_cell<std::array<float, 3>>(0x4347BD30u),
                         config.effects_settings().third_person_key.value.binding,
                         source.input_runtime().bitmap(),
                         source.data().native_cell<lagcomp::SetAbsoluteVector>(0x434688C8u),
                         network_fix,
                         config.other_esp_settings().overlays,
                         crosshair,
                         source.data().native_cell<void*>(0x4346A890u),
                         source.data().native_cell<std::uint8_t>(0x43468E58u)},
          local(local_bindings),
          query_bindings{source.data().native_cell<std::uint32_t>(0x43476904u),
                         source.data().native_cell<std::uintptr_t>(0x434674E0u),
                         source.data().native_cell<std::uint8_t>(0x4346907Cu),
                         source.data().native_cell<std::uint8_t>(0x43468E58u),
                         source.data().native_cell<std::uintptr_t>(0x4346756Cu),
                         field(source, 0x8B5586F6),
                         source.data().native_cell<void*>(0x4346A514u),
                         source.data().native_cell<std::uint8_t*>(0x43468FA4u),
                         field(source, 0x9D920E35),
                         source.data().native_cell<const std::int32_t*>(0x434757DCu),
                         source.data().native_cell<const std::int32_t*>(0x43475B2Cu),
                         source.data().native_cell<void*>(0x4346A894u),
                         source.data().native_cell<std::uintptr_t>(0x43468518u),
                         capture.sent_history()},
          queries(query_bindings),
          physics({history_owner.players().cache(), config.rage_settings().global.enabled.value,
                   config.anti_aim_global().enabled.value,
                   source.data().native_cell<std::uint32_t>(0x43468C2Cu),
                   source.data().native_cell<std::uint32_t>(0x43467F0Cu),
                   source.data().native_cell<std::uint32_t>(0x434757C4u),
                   source.data().native_cell<std::uint8_t*>(0x43468FA4u),
                   native_player_physics_hook_fields(source.offsets().globals)}),
          command_bindings{source.data().native_cell<void*>(0x43469264u),
                           config.effects_settings(),
                           source.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                           {field(source, 0xF58E6118), field(source, 0x3D52A1A7)}},
          command_hooks(command_bindings),
          simulation{source.data().native_cell<void*>(0x4346A514u),
                     nullptr,
                     source.data().native_cell<const std::int32_t*>(0x43468D4Cu),
                     engine.variables.at(0x4346794Cu),
                     source.prediction_observers(),
                     simulation_fields(source)},
          restricted{
              &config.rage_settings().global.enabled.value,
              &config.anti_aim_global().enabled.value,
              angles,
              engine.variables.at(0x4346794Cu),
              source.data().native_cell<void*>(0x4346A890u),
              history_owner.players().cache(),
              RestrictedCommandRing(source.data().native_cell<void*>(0x4346A894u)),
              source.data().native_cell<EntityCommandOwner*>(0x43475898u),
              &pointed<float>(source.data(), 0x4346A338u),
              &pointed<std::int32_t>(source.data(), 0x434678F8u),
              &pointed<float>(source.data(), 0x43467EB8u),
              reinterpret_cast<float*>(source.data().native_cell<std::byte*>(0x43467B04u) + 0x10),
              &source.data().native_cell<float>(0x43476B3Cu),
              &source.data().native_cell<float>(0x43476B40u),
              &source.data().native_cell<std::array<float, 3>>(0x4347BD30u),
              &source.prediction_observers(),
              nullptr,
              &source.data().native_cell<std::uint8_t>(0x43475BD3u),
              {0, 0, 0, nullptr, &source.data().native_cell<std::uint8_t*>(0x43467B24u)},
              update_engine,
              helpers,
              restricted_fields(source, history_owner.entity_runtime().offsets),
              0,
              true,
              true} {}
};
LocalAnimationRuntimeOwner::LocalAnimationRuntimeOwner(ClientRuntimeStorage& storage,
                                                       const EngineRuntimeBindings& engine,
                                                       ConfigurationRuntimeOwner& config,
                                                       HistoryRuntimeOwner& history,
                                                       CommandCaptureRuntimeOwner& capture,
                                                       effects::EffectsRuntimeOwner& effects)
    : data_(std::make_unique<Data>(storage, engine, config, history, capture, effects)) {}
LocalAnimationRuntimeOwner::~LocalAnimationRuntimeOwner() = default;
ClientRuntimeStorage& LocalAnimationRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
HistoryRuntimeOwner& LocalAnimationRuntimeOwner::history() const noexcept {
    return data_->history;
}
const RestrictedPlayerCommandBindings& LocalAnimationRuntimeOwner::restricted() const noexcept {
    return data_->restricted;
}
const animation::LocalAnimationBindings& LocalAnimationRuntimeOwner::helpers() const noexcept {
    return data_->helpers;
}
LocalPlayerAnimationHooks& LocalAnimationRuntimeOwner::local_hooks() noexcept {
    return data_->local;
}
PlayerCommandQueries& LocalAnimationRuntimeOwner::queries() noexcept {
    return data_->queries;
}
PlayerPhysicsHooks& LocalAnimationRuntimeOwner::physics() noexcept {
    return data_->physics;
}
PlayerCommandHooks& LocalAnimationRuntimeOwner::command_hooks() noexcept {
    return data_->command_hooks;
}
const prediction::PlayerSimulationBindings&
LocalAnimationRuntimeOwner::simulation() const noexcept {
    return data_->simulation;
}
effects::ViewDuckBindings& LocalAnimationRuntimeOwner::duck() noexcept {
    return data_->duck;
}
effects::ViewRuntime& LocalAnimationRuntimeOwner::view() noexcept {
    return data_->effects.view();
}
} //namespace skeet::client
