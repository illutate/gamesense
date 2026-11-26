#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/client/command_orchestrator.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/movement_hud_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/anti_aim/targets.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/esp/runtime_bindings.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::client {
namespace {
std::uint32_t offset(ClientRuntimeStorage& storage, std::uint32_t identity) {
    const auto& g = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(g, identity, row.xor_key_identity, row.initial_xor_key, 0)));
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(g, identity, row.xor_key_identity, row.initial_xor_key, 0)));
    throw std::invalid_argument("Target refresh field has no recovered offset recipe");
}
template <class T> T field(const void* value, std::uint32_t displacement) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(value) + displacement, sizeof result);
    return result;
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              PlayerRuntimeOwner& players, HistoryRuntimeOwner& history,
                              engine::SceneQueryRuntimeOwner& scene,
                              animation::RuntimeOwner& animation,
                              MovementHudRuntimeOwner& movement) {
    if (&engine.variables.data() != &storage.data() || &players.storage() != &storage ||
        &history.storage() != &storage || &history.players() != &players ||
        &scene.storage() != &storage || &scene.players() != &players ||
        &animation.storage() != &storage || &animation.players() != &players.cache() ||
        &movement.storage() != &storage)
        throw std::invalid_argument(
            "Target refresh must share the actual engine, history, player, scene and movement owners");
    for (const auto identity : {0x43468EA4u, 0x43467A8Cu, 0x4346A568u})
        if (!engine.variables.at(identity).get())
            throw std::invalid_argument("Target refresh ConVar is not acquired");
    auto& data = storage.data();
    if (!data.native_cell<void*>(0x43475B90u) || !data.native_cell<void*>(0x4346A3A4u) ||
        !data.native_cell<void*>(0x434678F8u))
        throw std::invalid_argument(
            "Target refresh requires live frame, rules and genuine trace-filter publications");
    return storage;
}
}
struct TargetRefreshRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    PlayerRuntimeOwner& players;
    HistoryRuntimeOwner& history;
    engine::SceneQueryRuntimeOwner& scene;
    animation::RuntimeOwner& animation;
    MovementHudRuntimeOwner& movement;
    const EngineRuntimeBindings& engine;
    esp::EnginePlayers resources;
    anti_aim::TargetSelectorOffsets selector;
    anti_aim::TargetScoredOffsets scored;
    anti_aim::TargetGeometryOffsets geometry;
    std::uint32_t rule_first, rule_second, rule_mode, local_time;
    Data(ClientRuntimeStorage& s, const EngineRuntimeBindings& e, PlayerRuntimeOwner& p,
         HistoryRuntimeOwner& h, engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& a,
         MovementHudRuntimeOwner& m)
        : storage(checked(s, e, p, h, q, a, m)), players(p), history(h), scene(q), animation(a),
          movement(m), engine(e), resources(esp::native_player_engine_bindings(s, e)),
          selector{offset(s, 0x434670CCu), offset(s, 0x4346A8E4u), offset(s, 0x43468A34u),
                   offset(s, 0x434685D4u)},
          scored{offset(s, 0x4346A60Cu), offset(s, 0x43467DD4u), offset(s, 0x43467908u),
                 offset(s, 0x43468C04u), offset(s, 0x434683C8u), offset(s, 0x43469344u) - 0x18u},
          geometry{a.bones().fields.studio_wrapper_32e4daf1, a.bones().fields.hitbox_set_c2b6fb10},
          rule_first(offset(s, 0x43468378u)), rule_second(offset(s, 0x4346836Cu)),
          rule_mode(offset(s, 0x43468BF4u)), local_time(offset(s, 0x43468D6Cu)) {}
};
TargetRefreshRuntimeOwner::TargetRefreshRuntimeOwner(ClientRuntimeStorage& s,
                                                     const EngineRuntimeBindings& e,
                                                     PlayerRuntimeOwner& p, HistoryRuntimeOwner& h,
                                                     engine::SceneQueryRuntimeOwner& q,
                                                     animation::RuntimeOwner& a,
                                                     MovementHudRuntimeOwner& m)
    : data_(std::make_unique<Data>(s, e, p, h, q, a, m)) {}
TargetRefreshRuntimeOwner::~TargetRefreshRuntimeOwner() = default;
ClientRuntimeStorage& TargetRefreshRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
void TargetRefreshRuntimeOwner::refresh(anti_aim::TargetState& target, void* player,
                                        const Command& command, std::uint32_t buttons,
                                        float yaw) const {
    auto& d = *data_;
    auto& data = d.storage.data();
    if (&target != &data.native_cell<anti_aim::TargetState>(0x4346DB14u))
        throw std::invalid_argument(
            "Target refresh must publish the canonical shared target state");
    //4335BA8E checks this before either selector or its engine/resource reads.
    if (*data.native_cell<const std::int32_t*>(0x434678F8u) == target.sequence)
        return;
    if (!player)
        throw std::invalid_argument("Target refresh requires its actual current player");
    const rage::TargetBindings targeting{player,
                                         data.native_cell<void*>(0x4346A890u),
                                         d.history.game_type(),
                                         d.history.teammates_are_enemies(),
                                         d.players.adjustments(),
                                         d.history.target_offsets()};
    const auto holder = data.native_cell<void**>(0x4346A3A4u);
    const auto rules = holder ? *holder : nullptr;
    const bool first = rules && field<std::uint8_t>(rules, d.rule_first) != 0;
    const bool second = first && field<std::uint8_t>(rules, d.rule_second) != 0;
    const anti_aim::TargetPredictionLimit prediction{
        rules != nullptr, first, second, second ? field<std::int32_t>(rules, d.rule_mode) : 0,
        data.native_cell<std::int32_t>(0x43469274u)};
    const anti_aim::TargetSelectorContext common{
        targeting,
        data.native_cell<void*>(0x4346A514u),
        data.native_cell<const std::int32_t*>(0x43469300u),
        anti_aim::TargetPlayerHistories(d.players.cache()),
        d.selector,
        data.native_cell<lagcomp::TickTimingSamples>(0x43478C70u),
        prediction,
        *data.native_cell<const float*>(0x4346A338u),
        field<float>(data.native_cell<void*>(0x43467B04u), 0x20),
        d.scene.queries().movement(),
        d.scene.queries().ballistics(),
        data.native_cell<std::uint8_t*>(0x434683D0u)};
    auto environment = d.movement.movement_environment();
    environment.interval = data.native_cell<const float*>(0x4346A338u);
    environment.entity_list = data.native_cell<void*>(0x4346A890u);
    environment.engine_trace_flag = data.native_cell<std::uint8_t*>(0x434683D0u);
    const auto& base = d.movement.movement_capture();
    const anti_aim::MovementCaptureInput movement{base.offsets, base.convars, *environment.interval,
                                                  &environment};
    const anti_aim::RawTargetTraceEntities entities{targeting.entity_list, d.scored};
    const anti_aim::LiveTargetScoredContext scored{
        common,
        command,
        buttons,
        yaw,
        d.resources,
        d.scored,
        d.geometry,
        data.native_cell<RageVector>(0x4347C9E0u),
        data.native_cell<HitboxMatrices>(0x4347C650u),
        d.animation.trigonometry(),
        data.native_cell<const std::int32_t*>(0x434678F8u),
        data.native_cell<const void* const*>(0x4346A3A4u),
        d.local_time,
        data.native_cell<void**>(0x43475B90u),
        entities,
        movement,
        environment};
    anti_aim::refresh_live_yaw_target(target, scored, d.engine.variables.at(0x43468EA4u).get());
}
} //namespace skeet::client
