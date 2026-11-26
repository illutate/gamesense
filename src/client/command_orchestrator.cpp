#include "skeet/client/command_orchestrator.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include "skeet/effects/free_look_owner.hpp"
#include "skeet/network/packet_control.hpp"
#include <bit>
#include <cstring>

namespace skeet::client {
namespace {
CommandCreateMoveRuntime* create_move_runtime;
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
bool command_player_rejected(const void* player, const CommandPlayerGates& gates) noexcept {
    return field<std::uint8_t>(player, gates.rejection_bytes[0]) != 0 ||
           field<std::uint8_t>(player, gates.rejection_bytes[1]) != 0;
}
bool command_player_restricted(const void* player, const CommandPlayerGates& gates) noexcept {
    if (field<std::int32_t>(player, gates.restricted_state) != 0)
        return true;
    const auto type = field<std::uint8_t>(player, gates.movement_type);
    return type == 10 || type == 8 ||
           (field<std::uint32_t>(player, gates.player_flags) & 0x40u) != 0;
}
void catch_up_command_packet(std::uint8_t& send, const std::uint8_t& packet_context,
                             const CommandPacketCatchupBindings& bindings) {
    if (!packet_context || send)
        return;
    auto* channel = engine::invoke<void*>(bindings.engine_client, 78);
    //Native captures the network-state pointer before calling the channel.
    const auto* state = bindings.network_state;
    if (!channel)
        return;
#if defined(_M_IX86) || defined(__i386__)
    //The native caller consumes ST0 without first rounding it to float32.
    const auto channel_time = engine::invoke<long double>(channel, 2);
#else
    const auto channel_time = static_cast<long double>(engine::invoke<float>(channel, 2));
#endif
    const auto difference =
        static_cast<double>(static_cast<long double>(field<double>(state, 0x108)) - channel_time);
    const float maximum = bindings.interval * 0.25f;
    if (difference > 0.0 && difference < static_cast<double>(maximum))
        send = 1;
}
std::int32_t query_command_budget(const CommandBudgetBindings& bindings) noexcept {
    if (bindings.game_rules && *bindings.game_rules) {
        const auto* rules = *bindings.game_rules;
        if (!field<std::uint8_t>(rules, bindings.rule_bf567619))
            return bindings.configured;
        if (field<std::uint8_t>(rules, bindings.rule_dda99ab5) &&
            field<std::int32_t>(rules, bindings.rule_b7ea8fda) == 1)
            return 3;
    }
    return 7;
}
void forward_original_command_angles(void* const& engine_client, void* const& prediction,
                                     Command& command, bool empty_tick) {
    engine::invoke<void>(engine_client, 19, &command.angles);
    if (empty_tick)
        engine::invoke<void>(prediction, 13, &command.angles);
}
void finalize_command_movement(CommandFrame& frame, Command& command, std::uint8_t& send,
                               const CommandButtonPolicy& policy,
                               const CommandTrigonometry& trigonometry,
                               const ScriptCommandEnvironment& environment,
                               std::int32_t last_outgoing, std::int32_t& last_shot) {
    frame.input_side_80 = command.movement.side_move;
    frame.input_forward_78 = command.movement.forward_move;
    reorient_command_movement(command, frame.angles_118, frame.state_104,
                              (frame.flags & 0x40000u) != 0, send == 0, policy, trigonometry);
    sanitize_command(command, (frame.flags & 8u) != 0, environment);
    enforce_command_choke_limit(send, frame.choked_15c, frame.choke_limit_fc);
    sanitize_final_command_angles(command);
    record_command_shot(frame.flags, command, last_outgoing, last_shot);
}
void finish_command_transaction(Command& command, std::uint8_t& engine_send,
                                std::uint8_t requested_packet,
                                const CommandPacketTransitionBindings& transition,
                                std::uint32_t& scheduler_flags,
                                const CommandEntityBindings& entities,
                                const CommandEntitySnapshot& saved_entities) {
    save_final_command(command);
    apply_command_packet_transition(requested_packet, engine_send, *(&engine_send + 1), transition);
    scheduler_flags &= ~0x20u;
    restore_command_entity_properties(entities, saved_entities);
}
namespace {
prediction::TimeTargets time_targets(const CommandMasterGlobals& globals) noexcept {
    return {reinterpret_cast<float*>(static_cast<std::byte*>(globals.engine_globals) + 0x14),
            globals.current_time, static_cast<std::uint8_t*>(globals.prediction) + 8};
}
std::int32_t player_tick_base(void* player, const CommandPlayerGates& offsets) noexcept {
    return field<std::int32_t>(player, offsets.tick_base);
}
bool prediction_paused(const CommandMasterGlobals& globals) noexcept {
    return field<std::uint8_t>(globals.prediction, 10) != 0;
}
void* command_weapon(void* player, const CommandAcceptedBindings& bindings) {
    const auto handle = field<std::uint32_t>(player, bindings.player_gates.active_weapon);
    return handle == UINT32_MAX ? nullptr
                                : engine::invoke<void*>(bindings.globals.entity_list, 4, handle);
}
SentCommandTiming sent_timing(const CommandAcceptedBindings& bindings) noexcept {
    const auto& timing = bindings.sent_timing;
    return {*bindings.globals.last_outgoing, field<std::int32_t>(timing.command_clock, 4),
            timing.acknowledged != nullptr,  timing.acknowledged ? *timing.acknowledged : 0,
            timing.sent != nullptr,          timing.sent ? *timing.sent : 0};
}
void refresh_sentinel_target(const CommandAcceptedBindings& bindings, void* player,
                             Command& command) {
    const auto& validation = bindings.capture.target_validation();
    auto& target = validation.target;
    //The outer lookup precedes the actual433ED5A8 validator's own lookup.
    if (target.handle != -1 &&
        engine::invoke<void*>(bindings.globals.entity_list, 4, target.handle) &&
        !command_target_valid(validation))
        anti_aim::clear_target_selection(target);
    bindings.sentinel_refresh.refresh(target, player, command, command.movement.buttons,
                                      command.angles[1]);
}
}
std::uint8_t run_accepted_command_pipeline(const CommandAcceptedBindings& bindings,
                                           CommandFrame& frame, void* player, float sample_time,
                                           Command& command, std::uint8_t& engine_send,
                                           std::uint8_t original_result) {
    bindings.order.require_sources();
    const auto& globals = bindings.globals;
    globals.scheduler_flags |= 0x20u;
    catch_up_command_packet(engine_send, *(&engine_send + 1), bindings.catchup);
    if (bindings.free_look) {
        const CommandAngleBindings view{globals.engine_client, globals.prediction,
                                        bindings.view_angles.environment};
        process_command_free_look(effects::free_look_owner_state(*bindings.free_look).command,
                                  command, bindings.free_look_key, bindings.order.services().input,
                                  view);
    }
    if (original_result)
        forward_original_command_angles(globals.engine_client, globals.prediction, command, false);
    advance_command_cycle_flags(globals.scheduler_flags, *globals.choked);
    CommandEntitySnapshot saved_entities;
    const CommandEntityBindings entities{globals.entity_list, bindings.entities.overridden_property,
                                         bindings.entities.replacement_value};
    save_command_entity_properties(entities, saved_entities);
    // Native ESP+1A is a separate mutable decision byte. Phases1/2 and the
    // frame's send pointer all address it; the saved engine EBX byte stays
    // unchanged until433D5888 commits that decision and reconciles packets.
    std::uint8_t send = engine_send;
    auto finish = [&] {
        finish_command_transaction(command, engine_send, send, bindings.packet_transition,
                                   globals.scheduler_flags, entities, saved_entities);
    };
    if (command_player_restricted(player, bindings.player_gates)) {
        prediction::TimeScope restricted_time(time_targets(globals), prediction_paused(globals),
                                              *globals.interval,
                                              player_tick_base(player, bindings.player_gates));
        process_restricted_player_command(prediction_history_shared(bindings.restricted_owner),
                                          player, &command, &send, true, bindings.restricted);
        restricted_time.restore();
        finish();
        return false;
    }
    //Native retains this first lookup's effects although it discards the value.
    command_weapon(player, bindings);
    prediction::TimeScope command_time(time_targets(globals), prediction_paused(globals),
                                       *globals.interval,
                                       player_tick_base(player, bindings.player_gates));
    auto* weapon = command_weapon(player, bindings);
    auto incoming_angles = command.angles;
    bindings.order.phase1(player, weapon, command, send);
    const auto saved_command = command;
    *globals.current_time =
        static_cast<float>(player_tick_base(player, bindings.player_gates)) * *globals.interval;
    if (std::bit_cast<std::uint32_t>(command.tick) == 0x7F7FFFFFu) {
        refresh_sentinel_target(bindings, player, command);
        command_time.restore();
        finish();
        return false;
    }
    incoming_angles = command.angles;
    weapon = command_weapon(player, bindings);
    {
        prediction::TimeScope phase2_time(time_targets(globals), prediction_paused(globals),
                                          *globals.interval,
                                          player_tick_base(player, bindings.player_gates));
        prediction::update_command_seed(command, bindings.seed_generator);
        bindings.order.phase2(player, weapon, command, incoming_angles, send);
        phase2_time.restore();
    }
    prepare_command_frame(frame, saved_command,
                          std::uint32_t(reinterpret_cast<std::uintptr_t>(&send)), *globals.choked,
                          command.number);
    const auto initial_limit =
        std::bit_cast<std::int32_t>(std::uint32_t(query_command_budget(bindings.budget)) - 1u);
    frame.choke_limit_fc = bindings.order.query_limit(initial_limit);
    const auto physical = bindings.capture.recapture(player);
    const auto timing = sent_timing(bindings);
    const auto capture = bindings.capture.capture(physical, timing);
    const auto captured =
        capture_initial_command_frame(frame, command, send, globals.scheduler_flags, sample_time,
                                      incoming_angles, bindings.bone_storage, capture);
    retain_command_attack_angles(frame, command, player, bindings.mouse, bindings.input);
    if (can_predict_command_copy(command, bindings.copy_gate_convar.get())) {
        CommandCopyOverrideScope outer(bindings.transform_override);
        Command predicted_command;
        const CommandButtonPolicy policy{bindings.inverted_commands,
                                         bindings.anti_aim_other.leg_movement.value,
                                         *globals.choked};
        prepare_prediction_command_copy(frame, command, predicted_command, send, policy,
                                        bindings.trigonometry, bindings.environment);
        const auto prediction_bindings = prediction::bind_command_prediction(
            bindings.prediction_bindings, globals.prediction, player, predicted_command,
            bindings.prediction_player_offsets);
        prediction::CommandPrediction prediction(prediction_bindings, player, &predicted_command,
                                                 std::uint32_t(predicted_command.random_seed));
        recapture_command_prediction(frame, command, prediction, outer, physical, bindings.eye);
    } else
        frame.flags |= 1u; //Native433D4612 skips the entire second prediction.
    auto current_weapon = [&] {
        return reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    };
    bindings.order.frame_phase(3, frame, player, command);
    bindings.order.frame_phase(4, frame, player, command);
    guard_changed_shot_packet(frame, command, send, current_weapon(), bindings.packet_angles);
    bindings.order.frame_phase(5, frame, player, command);
    guard_packet_angles(frame, command, send, player, current_weapon(), bindings.packet_angles);
    const CommandButtonPolicy policy{bindings.inverted_commands,
                                     bindings.anti_aim_other.leg_movement.value, *globals.choked};
    finalize_command_movement(frame, command, send, policy, bindings.trigonometry,
                              bindings.environment, *globals.last_outgoing, globals.last_shot);
    bindings.order.frame_phase(6, frame, player, command);
    record_sent_command_history(bindings.sent_history, command, send, sent_timing(bindings));
    bindings.order.frame_phase(7, frame, player, command);
    postprocess_command_after_phase7(bindings.postprocess, frame, player, command, send);
    bindings.order.frame_phase(8, frame, player, command);
    restore_command_prediction_cache(frame.flags, player, physical.offsets.common, captured.bones);
    command_time.restore();
    finish();
    return false;
}
std::uint8_t process_create_move_command(const CommandCreateMoveBindings& bindings,
                                         CommandFrame& frame, void* player, float sample_time,
                                         Command& command, std::uint8_t& send) {
    const auto& accepted = bindings.accepted;
    accepted.order.require_sources();
    require_empty_tick_sources(bindings.empty_tick_order);
    const CommandEntryBindings entry_bindings{
        accepted.globals.engine_client, bindings.entry.original_player_table,
        bindings.entry.channel_hook, bindings.entry.rejection_bytes};
    auto entry = enter_hooked_create_move(entry_bindings, player, sample_time, command);
    if (entry.path == CommandPath::engine_return)
        return entry.original_result;
    if (entry.path == CommandPath::empty_tick) {
        if (accepted.free_look) {
            const CommandAngleBindings view{accepted.globals.engine_client,
                                            accepted.globals.prediction,
                                            accepted.view_angles.environment};
            process_command_free_look(effects::free_look_owner_state(*accepted.free_look).command,
                                      command, accepted.free_look_key,
                                      accepted.order.services().input, view);
        }
        if (entry.original_result)
            forward_original_command_angles(accepted.globals.engine_client,
                                            accepted.globals.prediction, command, true);
        dispatch_empty_tick_command(bindings.empty_tick_order, player, sample_time, command,
                                    entry.original_result);
        return entry.original_result;
    }
    accepted.order.phase0(player, command);
    if (command_player_rejected(player, accepted.player_gates)) {
        if (command.number % 3 == 0)
            update_rejected_player(player, bindings.rejected_update);
        return entry.original_result;
    }
    return run_accepted_command_pipeline(accepted, frame, player, sample_time, command, send,
                                         entry.original_result);
}
std::uint8_t CommandCreateMoveRuntime::process(void* player, std::uint8_t* send, float sample_time,
                                               Command* command) {
    CommandFrame frame;
    return process_create_move_command(bindings, frame, player, sample_time, *command, *send);
}
void bind_create_move_runtime(CommandCreateMoveRuntime& runtime) noexcept {
    create_move_runtime = &runtime;
}
void unbind_create_move_runtime(CommandCreateMoveRuntime& runtime) noexcept {
    if (create_move_runtime == &runtime)
        create_move_runtime = nullptr;
}
#if defined(_M_IX86) || defined(__i386__)
std::uint8_t __fastcall create_move_command_hook(void* player, std::uint8_t* send,
                                                 float sample_time, Command* command) {
#else
std::uint8_t create_move_command_hook(void* player, std::uint8_t* send, float sample_time,
                                      Command* command) {
#endif
    return create_move_runtime->process(player, send, sample_time, command);
}
} //namespace skeet::client
