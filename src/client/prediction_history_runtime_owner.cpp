#include "skeet/client/prediction_history_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/local_animation_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/movement_hud_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/prediction_history_lifecycle.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/animation/local_prediction_pose.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/anti_aim/config.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
std::uint32_t field(ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& offsets = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                offsets, row.pointer_identity, row.xor_key_identity, salt, 0)));
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                offsets, row.pointer_identity, row.xor_key_identity, salt, 0)));
    if (salt == 0xF58E6118u)
        return std::uint32_t(
            std::ptrdiff_t(engine::DecodedOffset(offsets, 0x43468FD0u, 0x43467C70u, salt, 0)));
    throw std::invalid_argument(
        "Prediction history field has no recovered canonical offset recipe");
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage) {
    return {storage.owner_registries().at(0x43475B78u), storage.memory()};
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& config, LocalAnimationRuntimeOwner& local,
                              HistoryRuntimeOwner& history, MovementHudRuntimeOwner& movement,
                              animation::RuntimeOwner& animation,
                              CommandCaptureRuntimeOwner& capture) {
    auto& data = storage.data();
    const auto& command = local.restricted();
    const auto& entity = history.entity_runtime();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &local.storage() != &storage || &history.storage() != &storage ||
        &local.history() != &history || &movement.storage() != &storage ||
        &animation.storage() != &storage || &capture.storage() != &storage ||
        &animation.players() != &history.players().cache() ||
        &command.players != &history.players().cache() || &command.helpers != &local.helpers() ||
        command.enabled_43467224 != &config.rage_settings().global.enabled.value ||
        command.enabled_4346a740 != &config.anti_aim_global().enabled.value ||
        &command.history_owner != &data.native_cell<EntityCommandOwner*>(0x43475898u) ||
        &capture.globals().engine_client != &data.native_cell<void*>(0x4346A514u) ||
        command.offsets.animation.animation_state != entity.offsets.animation_state ||
        command.offsets.animation.pose_parameters != entity.offsets.pose_parameters ||
        entity.offsets.animation_state !=
            std::uint32_t(animation.bones().fields.animation_state_3d52a1a7))
        throw std::invalid_argument(
            "Prediction history requires the canonical local-animation, history, movement, capture and bone owners");
    const auto core = engine.interfaces.core();
    auto* const globals = data.native_cell<std::byte*>(0x43467B04u);
    if (!globals || !core.engine_client || !core.entity_list ||
        core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        !data.native_cell<void*>(0x4346A894u) ||
        data.native_cell<void*>(0x434678F8u) != globals + 4 ||
        data.native_cell<void*>(0x43467EB8u) != globals + 0x10 ||
        data.native_cell<void*>(0x4346A338u) != globals + 0x20 ||
        !data.native_cell<void*>(0x4346A334u) || !data.native_cell<void*>(0x43475B2Cu) ||
        !data.native_cell<void*>(0x43475BA4u) ||
        command.interval != data.native_cell<const float*>(0x4346A338u) ||
        command.current_time != data.native_cell<float*>(0x43467EB8u) ||
        animation.bones().entity_list != core.entity_list || entity.entity_list != core.entity_list)
        throw std::invalid_argument(
            "Prediction history requires its actual published engine and live clock/command aliases");
    return storage;
}
animation::LocalPredictionPoseOffsets pose_fields(const RestrictedPlayerCommandBindings& command) {
    const auto& o = command.offsets;
    return {std::uint32_t(o.field_36375c23), std::uint32_t(o.field_3309c0cf),
            std::uint32_t(o.field_e2fbe695), std::uint32_t(o.field_f58e6118),
            std::uint32_t(o.field_4e66bb34), std::uint32_t(o.field_2a671b42),
            o.animation.field_a2a8247f};
}
}
struct PredictionHistoryRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    PredictionHistoryStorage history;
    animation::LocalPredictionPoseBindings pose;
    PredictionHistoryBindings bindings;
    PredictionHistoryOwner command;
    PredictionHistoryRegistries registries;
    PredictionHistoryInitialization factory;
    Data(ClientRuntimeStorage& s, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, LocalAnimationRuntimeOwner& local,
         HistoryRuntimeOwner& remote_history, MovementHudRuntimeOwner& movement,
         animation::RuntimeOwner& animation, CommandCaptureRuntimeOwner& capture)
        : storage(checked(s, engine, config, local, remote_history, movement, animation, capture)),
          history(deferred_prediction_history_storage),
          pose{remote_history.entity_runtime(),
               pose_fields(local.restricted()),
               s.data().native_cell<lagcomp::TickTimingSamples>(0x43478C70u),
               movement.movement_capture(),
               movement.movement_environment(),
               movement.movement_command().bindings.command.collision,
               animation::LocalPredictionCommandRing(s.data().native_cell<void*>(0x4346A894u)),
               s.data().native_cell<void*>(0x434693F0u),
               s.data().native_cell<const std::int32_t*>(0x434678F8u),
               s.data().native_cell<float*>(0x43467EB8u),
               s.data().native_cell<const float*>(0x4346A338u),
               s.data().native_cell<std::uint8_t*>(0x43467B24u),
               s.data().native_cell<std::uint8_t>(0x43475BD3u),
               s.data().native_cell<void*>(0x43467204u),
               animation.bones(),
               s.data().native_cell<lagcomp::SetAbsoluteVector>(0x434688C8u),
               {s.data().native_cell<float>(0x4347C640u),
                s.data().native_cell<HitboxMatrices>(0x4347C650u),
                s.data().native_cell<RageVector>(0x4347C9E0u)},
               s.data().native_cell<const float*>(0x4346A334u)},
          bindings{local.restricted(),
                   pose,
                   s.data().native_cell<const std::int32_t*>(0x43475B2Cu),
                   std::ptrdiff_t(field(s, 0x9D920E35u)),
                   std::ptrdiff_t(field(s, 0xC865F10Eu)),
                   animation.bones().fields.angles_3b1f6ecc,
                   s.data().native_cell<lagcomp::SetAbsoluteVector>(0x434688C8u),
                   s.data().native_cell<engine::MemberFunction<void, std::int32_t>>(0x43468C10u)},
          command{history.shared(), history.redirect_from_44, history.redirect_to_48, bindings},
          registries{registry(s),           s.command_phases(),
                     s.empty_ticks(),       s.prediction_observers(),
                     s.entity_observers(),  s.data_update_observers(),
                     s.recharge_observers()},
          factory{registries,
                  s.configuration(),
                  {history, command, s.data().native_cell<const std::int32_t*>(0x43475BA4u),
                   s.data().native_cell<void*>(0x43467204u),
                   s.data().native_cell<PredictionHistoryOwner*>(0x434759B4u)}} {}
};
PredictionHistoryRuntimeOwner::PredictionHistoryRuntimeOwner(
    ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
    ConfigurationRuntimeOwner& config, LocalAnimationRuntimeOwner& local,
    HistoryRuntimeOwner& history, MovementHudRuntimeOwner& movement,
    animation::RuntimeOwner& animation, CommandCaptureRuntimeOwner& capture)
    : data_(std::make_unique<Data>(storage, engine, config, local, history, movement, animation,
                                   capture)) {}
PredictionHistoryRuntimeOwner::~PredictionHistoryRuntimeOwner() = default;
ClientRuntimeStorage& PredictionHistoryRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
PredictionHistoryStorage& PredictionHistoryRuntimeOwner::history_storage() noexcept {
    return data_->history;
}
PredictionHistoryOwner& PredictionHistoryRuntimeOwner::command() noexcept {
    return data_->command;
}
const animation::LocalPredictionPoseBindings& PredictionHistoryRuntimeOwner::pose() const noexcept {
    return data_->pose;
}
PredictionHistoryInitialization& PredictionHistoryRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void PredictionHistoryRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument(
            "Prediction history factory requires its canonical source scheduler");
    bind_prediction_history_initializer(sources, data_->factory);
}
} //namespace skeet::client
