#include "skeet/network/command_owner.hpp"
#include "skeet/network/packet_duck_live.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include <bit>
#include <cstring>
#include <intrin.h>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
Vector vector(const std::array<float, 3>& value) noexcept {
    return {value[0], value[1], value[2]};
}
void initialize_packet_state_prefix(FakeLagState& state) noexcept {
    state.tracked_target = -1;
    state.value_8c = -1;
    state.flags |= 1U;
}
void initialize_packet_state_tail(FakeLagState& state, std::uint32_t timestamp_low) noexcept {
    const auto cycle = std::int32_t(timestamp_low & 0x7fffffffU);
    state.target = 0;
    state.cycle = cycle;
    state.selected = 14;
}
float convar_float(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1c);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2c) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
FakeLagActivation activation(const PacketCommandOwner& owner) noexcept {
    return {owner.settings.enabled.value, owner.settings.hotkey.value.binding, owner.globals.input};
}
void* rules(const PacketCommandOwner& owner) noexcept {
    return owner.globals.shared_player ? *owner.globals.shared_player : nullptr;
}
bool freeze_period(const PacketCommandOwner& owner) noexcept {
    const auto player = rules(owner);
    return player && field<std::uint8_t>(player, owner.offsets.rule_d720aea6) != 0;
}
bool rule_gate(const PacketCommandOwner& owner) noexcept {
    const auto player = rules(owner);
    return !player || field<std::uint8_t>(player, owner.offsets.rule_bf567619) != 0;
}
void refresh_and_trigger(PacketCommandOwner& owner, const client::CommandFrame& frame, void* player,
                         client::Command& command, const PacketFrame& packet,
                         std::uint32_t& flags) {
    auto& world = owner.bindings.world;
    world.local_player = player;
    world.entity_list = owner.globals.entity_list;
    owner.bindings.target_refresh.refresh(owner.target, player, command,
                                          frame.previous_command.movement.buttons,
                                          frame.angles_118[1]);

    // These reads occur after the shared refresh, at the same boundary as
    // 433A0914. A preceding selector/engine call may have changed live globals.
    world.local_player = player;
    world.entity_list = owner.globals.entity_list;
    auto targeting = owner.bindings.targeting;
    targeting.local_player = player;
    targeting.entity_list = owner.globals.entity_list;
    auto weapons = owner.bindings.weapons;
    weapons.entity_list = owner.globals.entity_list;
    const FakeLagTriggerFrame trigger{packet,
                                      vector(frame.origin_168),
                                      frame.scalar_c0,
                                      frame.tick_base_c8,
                                      frame.weapon_kind_124,
                                      reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address)),
                                      command.movement.buttons,
                                      std::uint32_t(command.weapon_select)};
    FakeLagTriggerEnvironment trigger_environment{
        world,
        targeting,
        weapons,
        owner.globals.engine_client,
        *owner.globals.maximum_clients,
        targeting.offsets.active_weapon,
        rule_gate(owner),
        *owner.globals.last_outgoing,
        *owner.globals.real_time,
        *owner.globals.interval,
        convar_float(owner.bindings.movement.convars.gravity)};
    apply_fake_lag_triggers(owner.state, trigger, owner.target, trigger_environment, flags,
                            owner.globals.collision_result);
}
}

PacketFrame capture_packet_frame(const client::CommandFrame& frame) noexcept {
    return {frame.flags,
            frame.choke_limit_fc,
            frame.choked_15c,
            vector(frame.velocity_150),
            vector(frame.previous_velocity_180),
            vector(frame.origin_10c),
            frame.previous_speed_190};
}
void initialize_packet_command_state(FakeLagState& state) noexcept {
    initialize_packet_state_prefix(state);
    const auto timestamp_low = static_cast<std::uint32_t>(__rdtsc());
    initialize_packet_state_tail(state, timestamp_low);
}
void initialize_packet_command_state(FakeLagState& state, std::uint32_t timestamp_low) noexcept {
    initialize_packet_state_prefix(state);
    initialize_packet_state_tail(state, timestamp_low);
}
void process_packet_phase2(PacketCommandOwner& owner, void* player, void* weapon,
                           client::Command& command, const std::array<float, 3>& incoming_angles,
                           std::uint8_t& send_packet, std::uint32_t& flags) {
    static_cast<void>(weapon);
    static_cast<void>(incoming_angles);
    static_cast<void>(send_packet);
    const auto& offsets = owner.offsets;
    const auto choked = *owner.globals.choked_commands;
    if (choked == 0) {
        const PacketCommandContext context{choked,
                                           owner.globals.force_on_next_command != 0,
                                           owner.globals.last_command,
                                           command.number,
                                           field<Vector>(player, offsets.origin_a9ee6062),
                                           field<std::uint32_t>(player, offsets.duck_3309c0cf)};
        prepare_fake_lag_command(owner.state, context, flags);
    } else
        owner.state.recorded_choked = choked;
    update_packet_duck_live(owner, player, command.movement.buttons, flags);
}
void process_packet_phase4(PacketCommandOwner& owner, client::CommandFrame& frame, void* player,
                           client::Command& command, std::uint8_t& send_packet,
                           std::uint32_t& flags) {
    const auto packet = capture_packet_frame(frame);
    begin_fake_lag_tick(owner.state, packet, owner.globals.collision_result);
    if (freeze_period(owner))
        return;
    if (fake_lag_active(activation(owner))) {
        if (select_fake_lag_limit(owner.state, packet, *owner.globals.interval,
                                  owner.bindings.random))
            refresh_and_trigger(owner, frame, player, command, packet, flags);
        apply_fake_lag_send_gate(owner.state, packet, flags, send_packet);
    }
    finish_fake_lag_tick(owner.state, packet, owner.target.handle,
                         std::bit_cast<std::int32_t>(owner.target.fields_18[0]));
}
void process_packet_phase6(PacketCommandOwner& owner, client::CommandFrame& frame, void* player,
                           client::Command& command, std::uint8_t& send_packet,
                           std::uint32_t& flags) {
    static_cast<void>(player);
    static_cast<void>(send_packet);
    record_fake_lag_attack(owner.state, capture_packet_frame(frame), frame.weapon_id_d8,
                           frame.weapon_kind_124, command.movement.buttons,
                           *owner.globals.last_outgoing, owner.bindings.weapon_classification,
                           flags);
}
std::int32_t query_packet_command_limit(PacketCommandOwner& owner, std::int32_t limit) noexcept {
    return cap_fake_lag_commands(owner.state, limit);
}
} // namespace skeet::network
