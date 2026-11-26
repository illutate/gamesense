#include "skeet/anti_aim/command_owner.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include <cstring>

namespace skeet::anti_aim {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> T& slot(void* object, std::uint32_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}
const void* shared_player(const CommandOwner& owner) noexcept {
    return owner.globals.shared_player ? *owner.globals.shared_player : nullptr;
}
bool shared_guard(const CommandOwner& owner) noexcept {
    const auto* shared = shared_player(owner);
    return shared && field<std::uint8_t>(shared, owner.offsets.guard_d720aea6) != 0;
}
const void* entity_by_handle(void* object, std::int32_t handle) {
    return engine::invoke<const void*>(object, 4, handle);
}
class PlayerEdge final : public EdgeEngine {
    void* player_;
    CommandOwner& owner_;

public:
    PlayerEdge(void* player, CommandOwner& owner) : player_(player), owner_(owner) {}
    const MovementVector& hull_minimum() override {
        return *engine::invoke<const MovementVector*>(player_, 76);
    }
    const MovementVector& hull_maximum() override {
        return *engine::invoke<const MovementVector*>(player_, 77);
    }
    const MovementVector& origin() override {
        return *engine::invoke<const MovementVector*>(player_, 10);
    }
    MovementAngles view_angles() override {
        MovementAngles result;
        engine::invoke<void>(owner_.engine_client, 18, &result);
        return result;
    }
    std::uint32_t point_contents_world(const MovementVector& point, std::uint32_t mask) override {
        return engine::invoke<std::uint32_t>(owner_.engine_trace, 1, &point, mask);
    }
    void trace_ray(const MovementCollisionRay& ray, std::uint32_t mask,
                   MovementCollisionFilter filter, MovementCollisionTrace& trace) override {
        owner_.phase2.collision.trace_ray(ray, mask, filter, trace);
    }
    float surface_friction(std::int16_t property) override {
        return owner_.phase2.collision.surface_friction(property);
    }
};
struct PlayerPitch {
    void* player;
    const CommandOwnerPitchRandomBindings& imports;
    static void seed(void* context, std::int32_t seed_value) {
        const auto& self = *static_cast<PlayerPitch*>(context);
        self.imports.seed(self.imports.context, seed_value);
    }
    static std::int32_t integer(void* context, std::int32_t minimum, std::int32_t maximum) {
        const auto& self = *static_cast<PlayerPitch*>(context);
        return self.imports.integer(self.imports.context, minimum, maximum);
    }
    static std::int32_t code(void* context) {
        return engine::invoke<std::int32_t>(static_cast<PlayerPitch*>(context)->player, 122);
    }
};
AnglePlayer capture_angle_player(const CommandOwner& owner, const client::CommandFrame& frame,
                                 void* player) {
    const auto& o = owner.offsets;
    const auto& entity = owner.phase6.entity_offsets;
    const auto* animation = field<const void*>(player, entity.animation_state);
    AnglePlayer result{};
    if (frame.weapon_kind_124 == 9 || *owner.globals.mode == owner.state.value_158) {
        const auto* weapon = reinterpret_cast<const void*>(std::uintptr_t(frame.weapon_address));
        result.gates.weapon_state_e1df7e9b =
            field<std::uint8_t>(weapon, o.weapon_state_e1df7e9b) != 0;
        result.gates.weapon_time_3b93667b = field<float>(weapon, o.weapon_time_3b93667b);
    }
    result.gates.tick_base_3d10b94c = field<std::int32_t>(player, o.tick_base_3d10b94c);
    result.gates.movement_type_8b5586f6 = field<std::int32_t>(player, o.movement_type_8b5586f6);
    result.gates.field_7fcb7381 = field<std::uint8_t>(player, o.field_7fcb7381) != 0;
    result.gates.has_weapon_3d52a1a7 = animation != nullptr;
    result.gates.entity = reinterpret_cast<ProximityEntity>(player);
    result.edge_scalar_3eb71843 = field<float>(player, o.edge_3eb71843);
    result.state_7501cb91 = field<std::uint8_t>(player, o.movement_type_7501cb91);
    if (animation) {
        result.weapon_pitch_340 = field<float>(animation, 0x340);
        result.animation = {field<float>(animation, 0xA4),  field<float>(animation, 0xF8),
                            field<float>(animation, 0xFC),  field<float>(animation, 0x11C),
                            field<float>(animation, 0x334), field<float>(animation, 0x338)};
        result.feet_yaw_80 = field<float>(animation, 0x80);
    }
    return result;
}
}
AngleFrame capture_angle_frame(const client::CommandFrame& frame) noexcept {
    return {{frame.flags, frame.previous_command.movement.buttons, frame.choke_limit_fc,
             frame.choked_15c, frame.weapon_id_d8, frame.state_104, frame.weapon_kind_124,
             frame.previous_command.angles[0], frame.previous_command.angles[1]},
            frame.scalar_c0,
            frame.previous_command.movement.forward_move,
            frame.height_164,
            field<float>(&frame, 0x9C),
            frame.angle_b8,
            field<float>(&frame, 0xA0),
            frame.velocity_150[2],
            frame.origin_10c,
            frame.origin_168,
            frame.origin_dc,
            frame.command_number_ac};
}
void command_owner_phase2(CommandOwner& owner, void* player, void* weapon, client::Command& command,
                          const std::array<float, 3>& incoming_angles, std::uint8_t& send_packet) {
    (void)weapon; // native phase2's first stack argument is unused
    if (!owner.global.enabled.value) {
        owner.state.flags_130 &= ~2u;
        return;
    }
    const auto& o = owner.offsets;
    const auto* animation = field<const void*>(player, owner.phase6.entity_offsets.animation_state);
    const PrecalculationPlayer snapshot{
        field<std::uint32_t>(player, o.player_flags_e2fbe695),
        animation != nullptr,
        animation && *owner.globals.choked == 0 ? field<float>(animation, 0x80) : 0.0f,
        shared_guard(owner),
        field<std::uint8_t>(player, o.movement_type_7501cb91),
        field<MovementVector>(player, o.velocity_36375c23),
        field<float>(player, o.duck_3309c0cf),
        field<float>(player, o.rate_fb7e3404),
        field<std::uint8_t>(player, o.ducking_6b1a59b5) != 0};
    const auto& base = owner.phase2;
    auto live_environment = owner.environment;
    live_environment.interval = owner.globals.interval;
    live_environment.entity_list = owner.entity_list;
    live_environment.engine_trace_flag = owner.globals.trace_flag;
    const MovementCaptureInput movement{owner.movement.offsets, owner.movement.convars,
                                        *owner.globals.interval, &live_environment};
    auto roll_guard = base.roll_guard;
    roll_guard.shared_player = owner.globals.shared_player;
    roll_guard.player_guard = owner.offsets.rule_bf567619;
    const PrecalculationLiveFields live{o.duck_3309c0cf,       o.rate_fb7e3404,
                                        o.ducking_6b1a59b5,    o.player_flags_e2fbe695,
                                        &owner.globals.choked, &owner.globals.interval};
    const LivePrecalculationBindings bindings{
        player, movement, live_environment, base.collision, base.workspace, roll_guard, live};
    precalculate_live_command_angles(owner.global, owner.config, owner.precalculation, snapshot,
                                     *owner.globals.choked, command, incoming_angles, send_packet,
                                     bindings);
}
void command_owner_phase4(CommandOwner& owner, client::CommandFrame& frame, void* player,
                          client::Command& command, std::uint8_t& send_packet) {
    if (!owner.global.enabled.value || (frame.secondary_flags & 4u) ||
        (!owner.global.pitch.value && !owner.global.yaw.value && !owner.global.body_yaw.value)) {
        update_command_flags(frame.flags, owner.state.flags_130);
        return;
    }
    const auto* shared = shared_player(owner);
    const bool guard = shared_guard(owner);
    const EligibilityGuard eligibility_guard{
        shared != nullptr, guard, guard ? field<float>(shared, owner.offsets.clock_9f8e7f71) : 0.0f,
        *owner.globals.current_time};
    const auto& eligibility_source = owner.phase4.eligibility;
    const EligibilityBindings eligibility{
        eligibility_source.input_system, eligibility_source.engine_client,
        eligibility_source.hud_context, eligibility_source.find_hud};
    if (!command_eligible(
            {true, owner.global.pitch.value, owner.global.yaw.value, owner.global.body_yaw.value},
            {frame.flags, frame.secondary_flags}, eligibility_guard, owner.state.value_1c4,
            eligibility, owner.state.flags_130))
        return;
    const auto cached = command_uses_cached_angles(
        field<std::uint8_t>(player, owner.offsets.cached_a15c3105) != 0,
        field<std::int32_t>(player, owner.offsets.movement_type_8b5586f6), frame.flags,
        command.movement.buttons, owner.phase4.angles.proximity,
        reinterpret_cast<ProximityEntity>(player));
    AngleCycleState cycle{owner.state.flags_130, owner.state.value_190, owner.state.saved_yaw_198};
    begin_angle_cycle(cached, frame.choked_15c, owner.config.yaw_offsets.value[3], cycle);
    owner.state.flags_130 = cycle.flags_130;
    owner.state.value_190 = cycle.phase_190;
    owner.state.saved_yaw_198 = cycle.saved_yaw_198;

    if (owner.config.yaw_base.value == 1 || owner.config.freestanding.value ||
        owner.config.freestanding_body_yaw.value)
        owner.phase4.target_refresh.refresh(owner.globals.target, player, command,
                                            frame.previous_command.movement.buttons,
                                            field<float>(&frame, 0x11C));
    if (frame.state_104 == 8)
        return;
    owner.state.reference_yaw_258 = field<float>(owner.finisher.animation_1d8.data(), 0x80);

    auto angle_frame = capture_angle_frame(frame);
    const auto angle_player = capture_angle_player(owner, frame, player);
    const auto& base = owner.phase4.angles;
    const FreestandingContext freestanding{
        owner.globals.scheduler_flags, owner.globals.height_hotkey,
        owner.globals.input,           owner.globals.target.handle,
        owner.globals.target.position, {owner.entity_list, entity_by_handle}};
    const BodyFreestandingContext body_context{owner.globals.target.handle,
                                               owner.globals.target.position,
                                               owner.globals.shot_position, freestanding.entities};
    LiveBodyFreestandingSource body_bones{player, owner.phase6.bones,
                                          owner.phase6.entity_offsets.eye_angles,
                                          owner.phase6.entity_offsets.pose_parameters};
    const LiveBodyYawFreestandingBindings body{
        {owner.config.yaw_base.value, owner.parameters.value_148},
        body_context,
        body_bones,
        base.body_math,
        base.clipper,
        base.workspace};
    PlayerEdge edge(player, owner);
    PlayerPitch pitch_context{player, base.pitch};
    const PitchInterfaces pitch{&pitch_context, PlayerPitch::seed, PlayerPitch::integer,
                                PlayerPitch::code};
    const LiveAngleStageBindings angles{base.proximity,
                                        edge,
                                        base.ballistics,
                                        freestanding,
                                        body,
                                        pitch,
                                        base.random,
                                        owner.globals.trace_flag,
                                        owner.globals.last_body_command,
                                        owner.globals.shot_command,
                                        owner.globals.last_sent_command,
                                        owner.globals.shot_correction != 0};
    ActionGateGlobals globals{*owner.globals.mode,         *owner.globals.choked,
                              *owner.globals.interval,     owner.globals.scheduler_flags,
                              owner.globals.target.handle, freestanding.entities};
    process_angle_stage(owner.global, owner.config, owner.parameters, owner.state, angle_frame,
                        angle_player, globals, command, send_packet, angles);
    frame.flags = angle_frame.gates.flags;
    owner.globals.scheduler_flags = globals.command_flags;
}
void command_owner_phase6(CommandOwner& owner, client::CommandFrame& frame, void* player,
                          client::Command& command, std::uint8_t& send_packet) {
    if (!owner.global.enabled.value || !owner.global.body_yaw.value || !send_packet)
        return;
    const auto& base = owner.phase6;
    if (!field<const void*>(player, base.entity_offsets.animation_state))
        return;
    const auto& o = owner.offsets;
    const animation::AnimationAngleHistory history{owner.globals.pending_angles_active != 0,
                                                   *owner.globals.mode,
                                                   owner.globals.pending_angles};
    const animation::AnimationUpdateTiming timing{
        *owner.globals.sequence, *owner.globals.current_time, *owner.globals.interval, nullptr,
        &base.reset_byte};
    const auto update = lagcomp::native_history_animation_engine(base.animation_update);
    const LiveFinisherBindings bindings{player,
                                        slot<void*>(player, base.entity_offsets.animation_state),
                                        slot<float>(player, base.entity_offsets.field_a2a8247f),
                                        base.simulation_time,
                                        owner.state.build_bones_12c,
                                        slot<RageVector>(player, base.entity_offsets.eye_angles),
                                        base.final_angles,
                                        base.entity_offsets,
                                        history,
                                        timing,
                                        update,
                                        base.set_absolute_angles,
                                        base.bones};
    const FinisherPlayer snapshot{
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
        field<std::uint32_t>(player, o.flags_38ca5228),
        field<std::uint8_t>(player, o.field_043fa575),
        field<std::uint8_t>(player, o.field_334b460c), field<float>(player, o.field_86a5f8b3 - 4u)};
    finish_command_angles(owner.global.enabled.value != 0, owner.global.body_yaw.value,
                          owner.finisher, snapshot, frame.command_number_ac, command, send_packet,
                          bindings);
}
} // namespace skeet::anti_aim
