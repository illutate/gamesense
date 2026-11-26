#include "skeet/client/restricted_player_command.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include "restricted_player_command_internal.hpp"

namespace skeet::client {
using namespace restricted_detail;
namespace {
std::int32_t increment(std::int32_t value, int amount) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) + std::uint32_t(amount));
}
float recoil(void* player, const RestrictedPlayerCommandBindings& b) {
    const auto bits = lagcomp::read_prediction_convar(b.recoil_scale_convar);
    return std::bit_cast<float>(bits) * read<float>(player, b.offsets.field_1bae1168);
}
void* weapon(void* player, const RestrictedPlayerCommandBindings& b) {
    const auto handle = read<std::uint32_t>(player, b.offsets.weapon_handle);
    return handle == UINT32_MAX ? nullptr : engine::invoke<void*>(b.entity_list, 4, handle);
}
void transfer_prediction(void* player, const RestrictedCommandPrediction0x4C& r,
                         const RestrictedPlayerCommandBindings& b, bool restore_time = true) {
    const auto& o = b.offsets;
    if (restore_time)
        *b.globals_current_time = float(r.tick_base) * *b.interval;
    write(player, o.field_7a2d60c1, r.origin);
    write(player, o.field_36375c23, r.origin);
    write(player, o.field_4e66bb34, r.velocity);
    write(player, o.field_2a671b42, r.velocity);
    write(player, o.field_a9ee6062, r.velocity);
    write(player, o.field_3309c0cf, r.field_3309c0cf);
    write(player, o.animation.field_fb7e3404, r.field_fb7e3404);
    write(player, o.field_e2fbe695, r.flags);
    write(player, o.field_ac7f2913, r.field_24);
    write(player, o.field_7090d8dd, r.field_7090d8dd);
    write(player, o.field_cacaeecc, r.field_cacaeecc);
    write(player, o.field_a15c3105, r.field_a15c3105);
    write(player, o.field_ae838771, r.field_ae838771);
    write(player, o.field_d4248210, r.field_d4248210);
    write(player, o.field_a3abbb09, r.field_a3abbb09);
    write(player, o.field_7501cb91, r.field_7501cb91);
}
RestrictedCommandPrediction0x4C capture_prediction(void* player,
                                                   const RestrictedPlayerCommandBindings& b) {
    const auto& o = b.offsets;
    RestrictedCommandPrediction0x4C r{};
    r.tick_base = b.replay_restore_tick_scratch;
    r.origin = read<std::array<float, 3>>(player, o.field_36375c23);
    r.velocity = read<std::array<float, 3>>(player, o.field_2a671b42);
    r.field_3309c0cf = read<float>(player, o.field_3309c0cf);
    r.field_fb7e3404 = read<float>(player, o.animation.field_fb7e3404);
    r.flags = read<std::uint32_t>(player, o.field_e2fbe695);
    r.field_24 = read<std::uint8_t>(player, o.field_ac7f2913);
    r.field_7090d8dd = read<std::uint8_t>(player, o.field_7090d8dd);
    r.field_cacaeecc = read<std::uint8_t>(player, o.field_cacaeecc);
    r.field_a15c3105 = read<std::uint8_t>(player, o.field_a15c3105);
    r.field_ae838771 = read<float>(player, o.field_ae838771);
    r.field_d4248210 = read<std::uint32_t>(player, o.field_d4248210);
    r.field_a3abbb09 = read<std::uint32_t>(player, o.field_a3abbb09);
    r.field_7501cb91 = read<std::uint8_t>(player, o.field_7501cb91);
    return r;
}
bool can_replay(const RestrictedPlayerCommandOwner& owner, void* player, void* active_weapon,
                const Command& command, const RestrictedPlayerCommandBindings& b) {
    const auto first = increment(*owner.last_processed_command, 1);
    for (auto number = increment(command.number, -1); number >= first;
         number = increment(number, -1)) {
        const auto slot = std::uint32_t(number) % 150U;
        const auto& old = b.commands[slot];
        if (old.number != number || old.tick >= 0x7F7FFFFF)
            continue;
        if (owner.prediction_records[slot].command_number != number)
            return false;
        const auto& snapshot = owner.snapshots[slot];
        if (snapshot.command_number != number)
            continue;
        if (read<std::uint8_t>(&snapshot, 0x1F8) || read<void*>(&snapshot, 0x254) != player ||
            read<void*>(&snapshot, 0x258) != active_weapon)
            return false;
    }
    return true;
}
void replay_commands(RestrictedPlayerCommandOwner& owner, void* player, void* state,
                     const Command& current, PlayerState& cache, std::int32_t layer_count,
                     const RestrictedPlayerCommandBindings& b) {
    const auto& o = b.offsets;
    auto* descriptor = static_cast<std::byte*>(player) + o.animation.animation_layers;
    auto* layers = read<std::byte*>(descriptor, 0);
    if (layer_count >= 13) {
        for (std::ptrdiff_t i = 0; i < 13; ++i)
            for (std::ptrdiff_t word = 0; word < 7; ++word)
                write(layers, i * 0x38 + 0x14 + word * 4,
                      read<std::uint32_t>(&cache.native, 0x468 + i * 0x1C + word * 4));
    }
    const auto saved = capture_prediction(player, b);
    const auto saved_time = *b.current_time;
    auto restored_body_time = *b.body_yaw_time;
    const auto restored_body_yaw = read<float>(&cache.native, 0x118);
    bool restored = false;
    for (auto number = increment(*owner.last_processed_command, 1); number < current.number;
         number = increment(number, 1)) {
        const auto slot = std::uint32_t(number) % 150U;
        auto& command = b.commands[slot];
        if (command.tick == 0x7F7FFFFF)
            continue;
        auto& snapshot = owner.snapshots[slot];
        if (snapshot.command_number != number)
            continue;
        const auto& record = owner.prediction_records[slot];
        transfer_prediction(player, record, b);
        const auto* history =
            prediction::CommandHistoryView::from_owner(b.history_owner).find(number);
        const auto seed = increment(history ? read<std::int32_t>(history, 0) : -1, -1);
        if (!restored) {
            const auto previous_number = std::uint32_t(number) - 1U;
            const auto& previous = owner.snapshots[previous_number % 150U];
            if (std::uint32_t(previous.command_number) == previous_number)
                write(player, o.field_00a5bd68, read<std::uint8_t>(&previous, 4));
            std::memcpy(state, reinterpret_cast<const std::byte*>(&snapshot) + 0x1F4, 0x348);
            restored = true;
            if (read<std::int32_t>(descriptor, 0xC) > 7) {
                write(state, 0x19C, read<std::uint32_t>(layers, 0x1A0));
                write(state, 0x190, read<std::uint32_t>(layers, 0x1A8));
                write(state, 0x198, read<std::uint32_t>(layers, 0x1B4));
                write(state, 0x98, read<std::uint32_t>(layers, 0x17C));
            }
            const auto time = read<float>(&snapshot, 0x1F0);
            if (time != std::numeric_limits<float>::max())
                restored_body_time = time;
        } else {
            snapshot.command_number = number;
            write(&snapshot, 0x1F0, *b.body_yaw_time);
            std::memcpy(reinterpret_cast<std::byte*>(&snapshot) + 0x1F4, state, 0x348);
        }
        write(player, o.animation.field_a2a8247f, restored_body_yaw);
        *b.body_yaw = restored_body_yaw;
        const std::array<float, 3> angles{read<float>(&snapshot, 0x1E8),
                                          read<float>(&snapshot, 0x1EC), 0.0f};
        update_animation_step(owner, player, state, command, (record.field_04 & 0xFFU) != 0, seed,
                              layer_count, &angles, b);
        auto captured_animation = read<lagcomp::AnimationState>(&snapshot, 0x14);
        animation::capture_entity_animation(captured_animation, player, o.animation);
        write(&snapshot, 0x14, captured_animation);
        write(&snapshot, 0x1E4, read<std::uint32_t>(state, 0x80));
        write(&snapshot, 4, read<std::uint8_t>(player, o.field_00a5bd68));
    }
    // Native ESP+188 is uninitialized. In the canonical alias profile its
    // transient time write is overwritten below without an engine callback.
    // Elide that input only in this proven source profile. This does not
    // claim equivalence of the machine MXCSR exception-status flags.
    transfer_prediction(player, saved, b, !b.normalize_uninitialized_restore_time);
    *b.current_time = saved_time;
    *b.body_yaw_time = restored_body_time;
    *b.body_yaw = restored_body_yaw;
}
}
void process_restricted_player_command(RestrictedPlayerCommandOwner* owner, void* player,
                                       Command* command, std::uint8_t* send_packet,
                                       bool caller_flag, const RestrictedPlayerCommandBindings& b) {
    if (!player || !command || !send_packet || !owner)
        return;
    if (b.normalize_uninitialized_restore_time && b.current_time != b.globals_current_time)
        throw std::invalid_argument(
            "Restricted restore normalization requires the canonical current-time alias");
    const auto number = std::uint32_t(command->number), slot = number % 150U;
    const bool tagged = owner->command_tags[slot] == number;
    (void)tagged; //433F54D5 does not consume its fourth stack argument.
    auto& snapshot = owner->snapshots[slot];
    if (!*b.enabled_43467224 && !*b.enabled_4346a740) {
        snapshot.command_number = -1;
        return;
    }
    if (command->tick == 0x7F7FFFFF) {
        const auto previous_number = number - 1U;
        const auto& previous = owner->snapshots[previous_number % 150U];
        if (std::uint32_t(previous.command_number) == previous_number) {
            std::memcpy(&snapshot, &previous, sizeof snapshot);
            snapshot.command_number = command->number;
        }
        return;
    }
    const auto& o = b.offsets;
    const auto saved_recoil = read<std::uint32_t>(player, o.field_66146802);
    const auto saved_byte = read<std::uint8_t>(player, o.field_00a5bd68);
    const auto saved_movement = read<std::uint32_t>(player, o.field_52899df3);
    write(player, o.field_66146802, recoil(player, b));
    owner->prediction_records[slot].recoil_pitch = read<float>(player, o.field_66146802);
    auto angles = command->angles;
    animation::select_animation_angles(b.angle_history, command->number, angles);
    auto* state = read<void*>(player, o.animation.animation_state);
    write(&snapshot, 0x1F0, *b.body_yaw_time);
    if (state) {
        write(&snapshot, 0x1E8, angles[0]);
        write(&snapshot, 0x1EC, angles[1]);
        snapshot.command_number = command->number;
        std::memcpy(reinterpret_cast<std::byte*>(&snapshot) + 0x1F4, state, 0x348);
    }
    *engine::invoke<std::array<float, 3>*>(player, 170) = angles;
    if (state) {
        const auto index = engine::invoke<std::int32_t>(static_cast<std::byte*>(player) + 8, 10);
        auto& cache = *b.players.get(std::uint32_t(index));
        const auto layer_count = read<std::int32_t>(player, o.animation.animation_layers + 0xC);
        const auto saved_time = *b.current_time;
        const auto saved_command = read<std::uint32_t>(player, o.field_f58e6118);
        const auto saved_body = read<std::uint32_t>(player, o.animation.field_a2a8247f);
        if (caller_flag && can_replay(*owner, player, weapon(player, b), *command, b))
            replay_commands(*owner, player, state, *command, cache, layer_count, b);
        update_animation_step(*owner, player, state, *command, owner->command_tags[slot] == number,
                              read<std::int32_t>(player, o.tick_base), layer_count, nullptr, b);
        write(player, o.animation.field_0471e13c, std::uint32_t(0));
        write(player, o.animation.field_a2a8247f, saved_body);
        write(player, o.field_f58e6118, saved_command);
        *b.current_time = saved_time;
    }
    write(&snapshot, 4, read<std::uint8_t>(player, o.field_00a5bd68));
    if (state) {
        write(&snapshot, 8, read<std::uint32_t>(state, 0xEC));
        write(&snapshot, 0x1E4, read<std::uint32_t>(state, 0x80));
    }
    write(&snapshot, 0xC, *b.body_yaw_time);
    write(&snapshot, 0x10, *b.body_yaw);
    auto captured_animation = read<lagcomp::AnimationState>(&snapshot, 0x14);
    animation::capture_entity_animation(captured_animation, player, o.animation);
    write(&snapshot, 0x14, captured_animation);
    if (*send_packet) {
        auto sent = read<std::array<float, 3>>(player, o.field_f8205eba);
        for (auto& angle : sent)
            angle = normalize_movement_angle(angle);
        *b.sent_angles = sent;
    }
    write(player, o.field_52899df3, saved_movement);
    write(player, o.field_00a5bd68, saved_byte);
    write(player, o.field_66146802, saved_recoil);
}
} // namespace skeet::client
