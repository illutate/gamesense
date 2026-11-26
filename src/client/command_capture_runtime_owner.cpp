#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/engine/build_eye_adjustment_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/anti_aim/config.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
std::ptrdiff_t offset(ClientRuntimeStorage& storage, std::uint32_t identity) {
    const auto& globals = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(globals, identity, row.xor_key_identity,
                                         row.initial_xor_key, 0);
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(globals, identity, row.xor_key_identity,
                                         row.initial_xor_key, 0);
    throw std::invalid_argument("Command capture field has no recovered receive/datamap recipe");
}
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t identity) {
    auto* const object = data.native_cell<T*>(identity);
    if (!object)
        throw std::invalid_argument(
            "Command capture requires an initialized canonical engine alias");
    return *object;
}
CommandMouseOffsets mouse_fields(ClientRuntimeStorage& storage) {
    //043FA575 is the separately published +1 derivative of334B460C.
    return {
        offset(storage, 0x43468B50u),
        engine::DecodedOffset(storage.offsets().globals, 0x4346757Cu, 0x4346707Cu, 0x043FA575u, 0),
        offset(storage, 0x4346934Cu)};
}
CommandPlayerGates player_fields(ClientRuntimeStorage& storage) {
    return {{offset(storage, 0x43468E78u), offset(storage, 0x4346A5DCu)},
            offset(storage, 0x4346A6CCu),
            offset(storage, 0x43468BD4u),
            offset(storage, 0x434688D0u),
            offset(storage, 0x434693D8u),
            offset(storage, 0x43467A74u)};
}
weapon::FireStateOffsets fire_fields(ClientRuntimeStorage& storage) {
    //These two weapon datamap results are published separately at4348D65D,
    //before the ordinary derived-offset list in bindings_offset_storage.cpp.
    return {
        offset(storage, 0x4346908Cu),
        offset(storage, 0x4346A768u),
        offset(storage, 0x43467D40u),
        engine::DecodedOffset(storage.offsets().globals, 0x43468D68u, 0x43467558u, 0x836CB5DBu, 0),
        offset(storage, 0x4346799Cu),
        offset(storage, 0x434688B0u),
        offset(storage, 0x4346A7E0u),
        offset(storage, 0x4346A60Cu),
        offset(storage, 0x4346A80Cu),
        offset(storage, 0x43467A50u),
        offset(storage, 0x4346A6DCu),
        engine::DecodedOffset(storage.offsets().globals, 0x4346828Cu, 0x43468D2Cu, 0x9E2078A1u, 0),
        offset(storage, 0x43468FD8u),
        offset(storage, 0x434670A4u),
        offset(storage, 0x43467910u)};
}
CommandRecaptureOffsets recapture_fields(ClientRuntimeStorage& storage,
                                         const lagcomp::EntityStateRuntime& history) {
    return {history.offsets,
            offset(storage, 0x43467A74u),
            offset(storage, 0x43468BD4u),
            offset(storage, 0x43468A34u),
            offset(storage, 0x434682B8u),
            offset(storage, 0x43468354u),
            offset(storage, 0x4346A390u),
            offset(storage, 0x4346796Cu)};
}
CommandCaptureOffsets capture_fields(ClientRuntimeStorage& storage,
                                     const CommandMouseOffsets& mouse) {
    return {mouse,
            offset(storage, 0x4346746Cu),
            offset(storage, 0x434693D8u),
            offset(storage, 0x43467F74u),
            offset(storage, 0x4346A620u),
            offset(storage, 0x434688B0u)};
}
CommandEyeAdjustmentOffsets eye_fields(ClientRuntimeStorage& storage,
                                       const lagcomp::EntityStateRuntime& history) {
    return {history.offsets,
            offset(storage, 0x43468A34u),
            offset(storage, 0x4346A808u),
            offset(storage, 0x434688D0u),
            offset(storage, 0x4346908Cu),
            offset(storage, 0x43467E8Cu),
            offset(storage, 0x43468240u),
            offset(storage, 0x4346A390u),
            offset(storage, 0x434670CCu)};
}
ClientRuntimeStorage&
require_shared(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
               ConfigurationRuntimeOwner& configuration, PlayerRuntimeOwner& players,
               animation::RuntimeOwner& animation, const lagcomp::EntityStateRuntime& history) {
    auto& data = storage.data();
    const auto* command_clock = data.native_cell<const std::byte*>(0x43475A60u);
    if (&engine.variables.data() != &data || &configuration.storage() != &storage ||
        &players.storage() != &storage || &animation.storage() != &storage ||
        &animation.players() != &players.cache() ||
        history.engine != data.native_cell<void*>(0x4346A514u) ||
        history.entity_list != data.native_cell<void*>(0x4346A890u) ||
        &history.interval != &pointed<float>(data, 0x4346A338u) ||
        &history.receive_interval != &pointed<float>(data, 0x4346A338u) ||
        &history.frame != &pointed<std::int32_t>(data, 0x434678F8u) || !command_clock ||
        &history.command_tick != reinterpret_cast<const std::int32_t*>(command_clock + 4) ||
        &history.previous_command != &data.native_cell<const std::int32_t*>(0x43468F9Cu) ||
        &history.current_command != &data.native_cell<const std::int32_t*>(0x434670A8u))
        throw std::invalid_argument(
            "Command capture requires the shared canonical configuration, player, animation and history owners");
    const auto core = engine.interfaces.core();
    auto* const globals = data.native_cell<std::byte*>(0x43467B04u);
    if (!globals || core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        core.prediction != data.native_cell<void*>(0x43469264u) || !core.engine_client ||
        !core.entity_list || !core.prediction ||
        reinterpret_cast<const std::byte*>(data.native_cell<float*>(0x4346A338u)) !=
            globals + 0x20 ||
        reinterpret_cast<const std::byte*>(data.native_cell<float*>(0x43467EB8u)) !=
            globals + 0x10 ||
        !data.native_cell<void*>(0x4346A894u) || !data.native_cell<void**>(0x4346A3A4u) ||
        !data.native_cell<void**>(0x43468FF0u) || !data.native_cell<void*>(0x43475A60u))
        throw std::invalid_argument(
            "Command capture requires the initialized engine, CInput, global clock and client-state aliases");
    (void)pointed<std::int32_t>(data, 0x434757DCu);
    (void)pointed<std::int32_t>(data, 0x43475B2Cu);
    for (const auto cell : {0x43468EA4u, 0x43467A8Cu, 0x4346A42Cu, 0x43468528u, 0x4346747Cu,
                            0x4346A748u, 0x4346A330u, 0x4346794Cu})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument("Command capture requires its actual acquired ConVars");
    return storage;
}
}

struct CommandCaptureRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    ConfigurationRuntimeOwner& configuration;
    PlayerRuntimeOwner& players;
    animation::RuntimeOwner& animation;
    const lagcomp::EntityStateRuntime& history;
    CommandMasterGlobals globals;
    CommandPlayerGates gates;
    CommandMouseOffsets mouse;
    ScriptCommandEnvironment environment;
    CommandEntityBindings entities;
    CommandAngleBindings view;
    CommandPacketCatchupBindings catchup;
    CommandBudgetBindings budget;
    weapon::FireStateBindings fire;
    CommandRecaptureOffsets physical_fields;
    CommandCaptureOffsets capture_fields;
    CommandCaptureConvars convars;
    engine::NativeConVarReference recoil, copy_gate;
    CommandTargetValidation validation;
    CommandSentTimingBindings sent;
    SentCommandHistoryView history_view;
    engine::EyePoseParameterRuntime pose;
    CommandEyeAdjustmentSourceContext eye;
    std::vector<Matrix3x4> bones;

    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, PlayerRuntimeOwner& player_owner,
         animation::RuntimeOwner& animation_owner,
         const lagcomp::EntityStateRuntime& history_runtime)
        : storage(require_shared(source, engine, config, player_owner, animation_owner,
                                 history_runtime)),
          configuration(config), players(player_owner), animation(animation_owner),
          history(history_runtime), globals{source.data().native_cell<void*>(0x4346A514u),
                                            source.data().native_cell<void*>(0x4346A890u),
                                            source.data().native_cell<void*>(0x43469264u),
                                            source.data().native_cell<void*>(0x43467B04u),
                                            source.data().native_cell<float*>(0x4346A338u),
                                            source.data().native_cell<float*>(0x43467EB8u),
                                            source.data().native_cell<std::int32_t*>(0x434757DCu),
                                            source.data().native_cell<std::int32_t*>(0x43475B2Cu),
                                            source.data().native_cell<std::uint32_t>(0x434759C0u),
                                            source.data().native_cell<std::int32_t>(0x434759F8u)},
          gates(player_fields(source)), mouse(mouse_fields(source)),
          environment{&config.anti_untrusted().enabled.value,
                      source.data().native_cell<void**>(0x4346A3A4u), offset(source, 0x43468378u)},
          entities{source.data().native_cell<void*>(0x4346A890u), offset(source, 0x4346A808u),
                   source.data().native_cell<std::uint32_t*>(0x43469060u)},
          view{source.data().native_cell<void*>(0x4346A514u),
               source.data().native_cell<void*>(0x43469264u), environment},
          catchup{source.data().native_cell<void*>(0x4346A514u),
                  source.data().native_cell<void*>(0x43468E44u),
                  pointed<float>(source.data(), 0x4346A338u)},
          budget{source.data().native_cell<void**>(0x4346A3A4u), offset(source, 0x43468378u),
                 offset(source, 0x4346836Cu), offset(source, 0x43468BF4u),
                 source.data().native_cell<std::int32_t>(0x43469274u)},
          fire{source.data().native_cell<void**>(0x4346A3A4u),
               &pointed<float>(source.data(), 0x43467EB8u), fire_fields(source)},
          physical_fields(recapture_fields(source, history_runtime)),
          capture_fields(client::capture_fields(source, mouse)),
          convars{engine.variables.at(0x43468EA4u), engine.variables.at(0x43468254u),
                  engine.variables.at(0x43467A8Cu), engine.variables.at(0x4346A42Cu),
                  engine.variables.at(0x43468528u), engine.variables.at(0x4346747Cu),
                  engine.variables.at(0x4346A748u), engine.variables.at(0x4346A330u)},
          recoil(engine.variables.at(0x4346794Cu)), copy_gate(engine.variables.at(0x43468E94u)),
          validation{source.data().native_cell<anti_aim::TargetState>(0x4346DB14u),
                     player_owner.cache(),
                     source.data().native_cell<void*>(0x4346A890u),
                     &pointed<std::int32_t>(source.data(), 0x434678F8u),
                     source.data().native_cell<void**>(0x4346A3A4u),
                     offset(source, 0x43468D6Cu),
                     offset(source, 0x43468E78u),
                     offset(source, 0x434670CCu),
                     source.data().native_cell<void**>(0x43468FF0u),
                     offset(source, 0x43467CB8u),
                     offset(source, 0x4346A724u)},
          sent{source.data().native_cell<void*>(0x43475A60u),
               source.data().native_cell<std::int32_t*>(0x43468F9Cu),
               source.data().native_cell<std::int32_t*>(0x434670A8u)},
          history_view{source.data().native_cell<std::array<float, 3>>(0x43476414u),
                       source.data().native_cell<std::array<SentCommandRecord, 150>>(0x43476450u)},
          pose(source.data()),
          eye{eye_fields(source, history_runtime),
              &config.rage_settings().global.enabled.value,
              &config.anti_aim_global().enabled.value,
              reinterpret_cast<const std::uint32_t*>(&config.anti_aim_global().pitch.value),
              &source.data().native_cell<std::int32_t>(0x434759CCu),
              &source.data().native_cell<std::array<float, 3>>(0x434759D0u),
              source.data().native_cell<const std::uint32_t*>(0x43469060u),
              source.data().native_cell<std::array<std::uint32_t, 3>>(0x43474580u),
              animation_owner.bones(),
              animation_owner.trigonometry(),
              animation::native_sse_atan2,
              pose} {}
};

CommandCaptureRuntimeOwner::CommandCaptureRuntimeOwner(ClientRuntimeStorage& storage,
                                                       const EngineRuntimeBindings& engine,
                                                       ConfigurationRuntimeOwner& configuration,
                                                       PlayerRuntimeOwner& players,
                                                       animation::RuntimeOwner& animation,
                                                       const lagcomp::EntityStateRuntime& history)
    : data_(std::make_unique<Data>(storage, engine, configuration, players, animation, history)) {}
CommandCaptureRuntimeOwner::~CommandCaptureRuntimeOwner() = default;
ClientRuntimeStorage& CommandCaptureRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
const CommandMasterGlobals& CommandCaptureRuntimeOwner::globals() const noexcept {
    return data_->globals;
}
const CommandPlayerGates& CommandCaptureRuntimeOwner::player_gates() const noexcept {
    return data_->gates;
}
const CommandMouseOffsets& CommandCaptureRuntimeOwner::mouse_offsets() const noexcept {
    return data_->mouse;
}
const CommandRecaptureOffsets& CommandCaptureRuntimeOwner::physical_offsets() const noexcept {
    return data_->physical_fields;
}
const CommandCaptureOffsets& CommandCaptureRuntimeOwner::capture_offsets() const noexcept {
    return data_->capture_fields;
}
const weapon::FireStateBindings& CommandCaptureRuntimeOwner::firing() const noexcept {
    return data_->fire;
}
const CommandEntityBindings& CommandCaptureRuntimeOwner::entities() const noexcept {
    return data_->entities;
}
const ScriptCommandEnvironment& CommandCaptureRuntimeOwner::angle_environment() const noexcept {
    return data_->environment;
}
const CommandAngleBindings& CommandCaptureRuntimeOwner::view_angles() const noexcept {
    return data_->view;
}
const CommandPacketCatchupBindings& CommandCaptureRuntimeOwner::packet_catchup() const noexcept {
    return data_->catchup;
}
const CommandBudgetBindings& CommandCaptureRuntimeOwner::budget() const noexcept {
    return data_->budget;
}
const CommandTargetValidation& CommandCaptureRuntimeOwner::target_validation() const noexcept {
    return data_->validation;
}
const CommandSentTimingBindings& CommandCaptureRuntimeOwner::sent_timing_bindings() const noexcept {
    return data_->sent;
}
const CommandEyeAdjustmentSourceContext&
CommandCaptureRuntimeOwner::eye_adjustment() const noexcept {
    return data_->eye;
}
engine::NativeConVarReference CommandCaptureRuntimeOwner::copy_gate() const noexcept {
    return data_->copy_gate;
}
std::vector<Matrix3x4>& CommandCaptureRuntimeOwner::bone_storage() noexcept {
    return data_->bones;
}
SentCommandHistoryView& CommandCaptureRuntimeOwner::sent_history() noexcept {
    return data_->history_view;
}
CommandRecaptureContext CommandCaptureRuntimeOwner::recapture(void* player) const {
    if (!player)
        throw std::invalid_argument("Command physical capture requires the actual accepted player");
    auto& state = *data_;
    return {player,
            state.globals.entity_list,
            state.physical_fields,
            state.fire,
            state.recoil,
            &state.storage.data().native_cell<std::uint8_t>(0x434693D4u),
            state.animation.trigonometry(),
            state.storage.data().native_cell<std::array<float, 3>>(0x43475BD4u)};
}
CommandCaptureContext CommandCaptureRuntimeOwner::capture(const CommandRecaptureContext& physical,
                                                          const SentCommandTiming& timing) const {
    auto& state = *data_;
    if (!physical.player || &physical.fire != &state.fire ||
        &physical.trigonometry != &state.animation.trigonometry() ||
        physical.entity_list != state.globals.entity_list ||
        &physical.published_origin !=
            &state.storage.data().native_cell<std::array<float, 3>>(0x43475BD4u))
        throw std::invalid_argument(
            "Command capture requires this owner's current canonical physical view");
    const auto& settings = state.configuration.rage_settings().global;
    return {physical,
            state.environment,
            state.capture_fields,
            state.convars,
            state.globals.engine_client,
            settings.duck_peek_assist.value.binding,
            state.storage.input_runtime().bitmap(),
            &settings.reduce_aim_step.value,
            &settings.aim_step_limit.value,
            &state.storage.data().native_cell<float>(0x43469254u),
            state.storage.data().native_cell<network::LatencySamples>(0x43478C70u),
            timing,
            state.validation};
}
} //namespace skeet::client
