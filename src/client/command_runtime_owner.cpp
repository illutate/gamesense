#include "skeet/client/command_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/local_animation_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/shared_esp_runtime_owner.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/rage/settings.hpp"
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
    throw std::invalid_argument("Command runtime field has no recovered receive/datamap recipe");
}
ClientRuntimeStorage&
require_shared(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
               ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history,
               CommandCaptureRuntimeOwner& capture, animation::RuntimeOwner& animation) {
    auto& data = storage.data();
    const auto& globals = capture.globals();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &history.storage() != &storage || &capture.storage() != &storage ||
        &animation.storage() != &storage || &animation.players() != &history.players().cache() ||
        &globals.engine_client != &data.native_cell<void*>(0x4346A514u) ||
        &globals.entity_list != &data.native_cell<void*>(0x4346A890u) ||
        &globals.prediction != &data.native_cell<void*>(0x43469264u) ||
        &globals.interval != &data.native_cell<float*>(0x4346A338u) ||
        &globals.last_outgoing != &data.native_cell<std::int32_t*>(0x43475B2Cu) ||
        !globals.interval || &history.entity_runtime().interval != globals.interval)
        throw std::invalid_argument(
            "Command runtime requires the shared canonical engine, configuration, history, capture and animation owners");
    const auto& command = engine.command;
    if (!command.seed_generator || !command.move_helper || !command.random_seed ||
        !command.prediction_player || !command.flag_head ||
        command.seed_generator != data.native_cell<prediction::SeedGenerator>(0x434679ACu) ||
        command.move_helper != data.native_cell<void*>(0x43468EC8u) ||
        command.random_seed != data.native_cell<std::int32_t*>(0x43468328u) ||
        command.prediction_player != data.native_cell<void**>(0x434693A4u) ||
        command.flag_head != data.native_cell<prediction::PredictionFlagNode**>(0x43467C58u) ||
        !data.native_cell<void*>(0x4346A7F8u) || !data.native_cell<void*>(0x4346A894u))
        throw std::invalid_argument(
            "Command runtime requires the actual published command engine bindings");
    for (const auto cell : {0x43469008u, 0x434685A0u, 0x43467950u, 0x43467988u})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument(
                "Command runtime requires its actual acquired prediction and mouse ConVars");
    return storage;
}
}

struct CommandRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    ConfigurationRuntimeOwner& configuration;
    HistoryRuntimeOwner& history;
    CommandCaptureRuntimeOwner& capture;
    prediction::CommandPredictionConVars prediction_variables;
    prediction::CommandPlayerOffsets prediction_fields;
    prediction::CommandBindings prediction;
    CommandPacketAngleBindings packet_angles;
    CommandWeaponPostprocessBindings postprocess;
    CommandPacketTransitionBindings transition;
    CommandTrigonometry trigonometry;

    struct Complete {
        CommandEntryBindings entry;
        CommandAcceptedBindings accepted;
        CommandCreateMoveBindings bindings;
        CommandCreateMoveRuntime runtime;

        Complete(Data& state, ChannelHookState& channel,
                 const RestrictedPlayerCommandBindings& restricted,
                 TargetRefreshRuntimeOwner& refresh, const RejectedPlayerUpdateContext& rejected)
            // The production framing resolves virtual289 from this call's
            // receiver metadata after channel restoration. The original-table
            // field belongs exclusively to enter_create_move's standalone API.
            : entry{state.capture.globals().engine_client, nullptr, &channel,
                    state.capture.player_gates().rejection_bytes},
              accepted{
                  state.capture.globals(),
                  state.capture.player_gates(),
                  state.capture.entities(),
                  state.capture.packet_catchup(),
                  state.storage.data().native_cell<effects::FreeLookOwner*>(0x43475A64u),
                  state.configuration.free_look().key.value.binding,
                  state.capture.view_angles(),
                  state.capture.budget(),
                  state.storage.command_phases(),
                  state.capture,
                  state.capture.eye_adjustment(),
                  state.capture.bone_storage(),
                  state.capture.mouse_offsets(),
                  state.storage.data().native_cell<void*>(0x4346A894u),
                  state.capture.copy_gate(),
                  state.storage.data().native_cell<std::uint8_t*>(0x434683D0u),
                  state.storage.data().native_cell<std::array<std::uint32_t, 150>>(0x43476090u),
                  state.configuration.anti_aim_other(),
                  state.trigonometry,
                  state.capture.angle_environment(),
                  state.prediction,
                  state.prediction_fields,
                  state.storage.data().native_cell<prediction::SeedGenerator>(0x434679ACu),
                  state.packet_angles,
                  state.capture.sent_history(),
                  state.capture.sent_timing_bindings(),
                  state.postprocess,
                  state.transition,
                  state.storage.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                  restricted,
                  refresh},
              bindings{entry, accepted, state.storage.empty_ticks(), rejected}, runtime{bindings} {}
    };
    std::unique_ptr<Complete> completed;

    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history_owner,
         CommandCaptureRuntimeOwner& capture_owner, animation::RuntimeOwner& animation)
        : storage(require_shared(source, engine, config, history_owner, capture_owner, animation)),
          configuration(config), history(history_owner), capture(capture_owner),
          prediction_variables{engine.variables.at(0x43469008u), engine.variables.at(0x434685A0u)},
          prediction_fields{engine::DecodedOffset(source.offsets().globals, 0x43468FD0u,
                                                  0x43467C70u, 0xF58E6118u, 0),
                            offset(source, 0x434671C8u)},
          prediction{source.data().native_cell<void*>(0x43469264u),
                     source.data().native_cell<void*>(0x43468EC8u),
                     source.data().native_cell<void*>(0x4346A7F8u),
                     &source.data().native_cell<std::byte>(0x43478CD8u),
                     engine.command.flag_head,
                     // The six player/command/engine-local targets are bound by the
                     // existing bind_command_prediction at each actual scope entry.
                     {nullptr, nullptr, nullptr,
                      reinterpret_cast<std::uint32_t*>(engine.command.random_seed),
                      reinterpret_cast<std::uint32_t*>(engine.command.prediction_player), nullptr,
                      nullptr, nullptr, nullptr, nullptr},
                     &prediction_variables},
          packet_angles{&config.rage_settings().global.enabled.value,
                        &config.rage_settings().global.silent_aim.value,
                        &config.settings_menu().hold_aim,
                        offset(source, 0x4346908Cu),
                        capture_owner.mouse_offsets().player_flags,
                        capture_owner.mouse_offsets().player_state_043fa575,
                        capture_owner.mouse_offsets().player_state_334b460c,
                        source.data().native_cell<std::array<std::uint32_t, 3>>(0x43474580u),
                        source.data().native_cell<void*>(0x4346A894u)},
          postprocess{source.data().native_cell<void*>(0x4346A894u),
                      engine.variables.at(0x43467950u), engine.variables.at(0x43467988u),
                      &source.data().native_cell<std::array<float, 3>>(0x4347A4F0u),
                      capture_owner.mouse_offsets()},
          transition{
              config.rage_settings().global.enabled.value,
              config.rage_settings().global.anti_aim_correction.value,
              source.data().native_cell<void*>(0x4346A514u),
              source.data().native_cell<void**>(0x4346A3A4u),
              source.offsets().globals.pointers.at(0x43468378u),
              source.offsets().globals.keys.at(0x43468244u),
              source.data().native_cell<std::uint32_t*>(0x43475B2Cu),
              source.data().native_cell<std::array<StartupAcknowledgement, 150>>(0x43475BE0u)},
          trigonometry{animation.trigonometry().sine, animation.trigonometry().cosine} {}
};

CommandRuntimeOwner::CommandRuntimeOwner(ClientRuntimeStorage& storage,
                                         const EngineRuntimeBindings& engine,
                                         ConfigurationRuntimeOwner& config,
                                         HistoryRuntimeOwner& history,
                                         CommandCaptureRuntimeOwner& capture,
                                         animation::RuntimeOwner& animation)
    : data_(std::make_unique<Data>(storage, engine, config, history, capture, animation)) {}
CommandRuntimeOwner::~CommandRuntimeOwner() {
    if (data_->completed)
        unbind_create_move_runtime(data_->completed->runtime);
}
ClientRuntimeStorage& CommandRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
CommandCaptureRuntimeOwner& CommandRuntimeOwner::capture() const noexcept {
    return data_->capture;
}
HistoryRuntimeOwner& CommandRuntimeOwner::history() const noexcept {
    return data_->history;
}
const prediction::CommandBindings& CommandRuntimeOwner::prediction() const noexcept {
    return data_->prediction;
}
const prediction::CommandPlayerOffsets&
CommandRuntimeOwner::prediction_player_offsets() const noexcept {
    return data_->prediction_fields;
}
const CommandPacketAngleBindings& CommandRuntimeOwner::packet_angles() const noexcept {
    return data_->packet_angles;
}
const CommandWeaponPostprocessBindings& CommandRuntimeOwner::postprocess() const noexcept {
    return data_->postprocess;
}
const CommandPacketTransitionBindings& CommandRuntimeOwner::packet_transition() const noexcept {
    return data_->transition;
}
const CommandTrigonometry& CommandRuntimeOwner::trigonometry() const noexcept {
    return data_->trigonometry;
}
bool CommandRuntimeOwner::complete() const noexcept {
    return bool(data_->completed);
}
void CommandRuntimeOwner::complete(engine::HookRuntimeOwner& hooks,
                                   LocalAnimationRuntimeOwner& local_animation,
                                   TargetRefreshRuntimeOwner& refresh,
                                   SharedEspRuntimeOwner& shared_esp) {
    auto& state = *data_;
    auto& data = state.storage.data();
    const auto& restricted = local_animation.restricted();
    const auto& rejected = shared_esp.rejected_player_update();
    if (state.completed)
        throw std::logic_error("Command runtime is already complete");
    if (&hooks.storage() != &state.storage || &refresh.storage() != &state.storage ||
        &shared_esp.storage() != &state.storage || &local_animation.storage() != &state.storage ||
        &local_animation.history() != &state.history ||
        &restricted.players != &state.history.players().cache() ||
        restricted.interval != state.capture.globals().interval ||
        restricted.enabled_43467224 != &state.configuration.rage_settings().global.enabled.value ||
        restricted.entity_list != state.capture.globals().entity_list ||
        &rejected.owner != &data.native_cell<RejectedPlayerUpdateOwner*>(0x434758D0u) ||
        &rejected.cache != &state.history.players().cache() ||
        &rejected.players != &state.history.players().adjustments() ||
        &rejected.entity_list != &data.native_cell<void*>(0x4346A890u) ||
        &rejected.game_rules != &data.native_cell<void**>(0x4346A3A4u) ||
        &rejected.blocked != &data.native_cell<const std::int32_t*>(0x434757DCu) ||
        &rejected.tick_interval != state.capture.globals().interval)
        throw std::invalid_argument(
            "Command completion requires the actual shared hook, restricted-animation, target-refresh and SharedESP contexts");
    auto& channel = hooks.channel_state(); //throws if its real producer is absent
    if (&channel.expected_table != &data.native_cell<void**>(0x43475B0Cu) ||
        &channel.replacement_table != &data.native_cell<void**>(0x43475B14u) ||
        &channel.installed_channel != &data.native_cell<void*>(0x43475B08u))
        throw std::invalid_argument(
            "Command entry requires the canonical process channel hook state");
    state.completed =
        std::make_unique<Data::Complete>(state, channel, restricted, refresh, rejected);
}
CommandCreateMoveRuntime& CommandRuntimeOwner::runtime() const {
    if (!data_->completed)
        throw std::logic_error("Command runtime is missing required late source contexts");
    return data_->completed->runtime;
}
void CommandRuntimeOwner::bind_callback() {
    bind_create_move_runtime(runtime());
}
} //namespace skeet::client
