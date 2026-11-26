#include "skeet/client/entity_command_owner.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/client/verified_command.hpp"
#include "skeet/animation/entity_update.hpp"
#include "skeet/rage/shot_feedback.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
const Command* verified_command(const EntityCommandBindings& bindings, std::int32_t number) {
    // Full 4334116B uses CInput +F4 and stride 68, distinct from the +F0 ring.
    const auto* entries = read<const VerifiedCommand*>(bindings.input, 0xF4);
    const auto& command = entries[std::uint32_t(number) % 150U].command;
    return command.number == number ? &command : nullptr;
}
prediction::CommandHistoryState* acknowledged_history(EntityCommandOwner& owner,
                                                      std::int32_t number) {
    // Full 433F0944 uses signed remainder and returns entry +4.
    auto& entry = owner.state.history[number % 150];
    return entry.command_number == number ? &entry.state : nullptr;
}
float recoil_scale(const void* variable) noexcept {
    for (;;) {
        const auto* parent = read<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    const auto bits = read<std::uint32_t>(variable, 0x2C) ^
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
    return std::bit_cast<float>(bits);
}
void confirm_animation_angles(EntityCommandOwner& owner, bool confirmed) {
    const auto& bindings = owner.bindings;
    auto& history = *bindings.angle_confirmation;
    bindings.angle_history.set_active(confirmed);
    auto count = history.count, index = history.index;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (history.values[(index + i) % 3U] != std::uint8_t(confirmed)) {
            count = 0;
            index = 0;
            history.count = 0;
            break;
        }
    }
    if (count != 3U)
        history.count = ++count;
    history.index = (index + 2U) % 3U;
    history.values[history.index] = std::uint8_t(confirmed);
}
void check_choked_shot_angles(EntityCommandOwner& owner, void* player) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    if (bindings.angle_confirmation->count == 3U)
        return;
    if (owner.state.last_choked_shot <= *bindings.old_acknowledged)
        return;
    const auto first_outgoing = engine::invoke<std::int32_t>(bindings.engine_client, 205);
    if (owner.state.last_choked_shot >= first_outgoing ||
        read<std::uint8_t>(player, offsets.rejected_9d920e35))
        return;

    // The second virtual query and both exact-number verified lookups retain
    // their native order; the interface can change its answer between calls.
    const auto outgoing = engine::invoke<std::int32_t>(bindings.engine_client, 205);
    const auto* latest = verified_command(bindings, outgoing);
    if (!latest || std::bit_cast<std::uint32_t>(latest->tick) == 0x7F7FFFFFU)
        return;
    const auto latest_pitch = entity_data_update_quantized_angle(latest->angles[0]);
    const auto latest_yaw = entity_data_update_quantized_angle(latest->angles[1]);
    const auto* fired = verified_command(bindings, owner.state.last_choked_shot);
    if (!fired || std::bit_cast<std::uint32_t>(fired->tick) == 0x7F7FFFFFU)
        return;
    const auto fired_pitch = entity_data_update_quantized_angle(fired->angles[0]);
    const auto fired_yaw = entity_data_update_quantized_angle(fired->angles[1]);
    if (fired_pitch == latest_pitch && fired_yaw == latest_yaw)
        return;
    const auto index = engine::invoke<std::uint32_t>(bindings.engine_client, 12);
    const auto* cached = bindings.players->get(index);
    const auto cached_pitch = read<float>(&cached->native, 0x1A8);
    if (latest_pitch == cached_pitch && latest_yaw == cached->native.reference_yaw)
        confirm_animation_angles(owner, false);
    else if (fired_pitch == cached_pitch && fired_yaw == cached->native.reference_yaw)
        confirm_animation_angles(owner, true);
}
}

void finish_entity_data_update_tail(EntityCommandOwner& owner, void* player,
                                    const EntityDataUpdateWork& work) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    if (work.acknowledged_delta > 0) {
        if (work.acknowledged_delta > 1)
            check_choked_shot_angles(owner, player);
    } else if (*bindings.shot_recording_enabled) {
        const auto saved_pitch = std::bit_cast<float>(owner.state.data_update.recoil_angles[0]);
        if (work.captured_tick_base != work.current_tick_base ||
            saved_pitch != read<float>(player, offsets.vector_1bae1168))
            invalidate_command_prediction(bindings.prediction);
    }

    if (work.field_7f5ed2d0_changed) {
        if (auto* record = acknowledged_history(owner, work.acknowledged_command)) {
            write(record, 0x40, read<std::uint32_t>(player, offsets.field_7f5ed2d0));
            invalidate_command_prediction(bindings.prediction);
        }
    }
    if (work.acknowledged_delta <= 0 && work.captured_tick_base == work.current_tick_base)
        return;

    // Native 43405A2B reloads the live ring base and adds the signed slot
    // offset saved at 43404CED. Its first word is deliberately not checked.
    auto& current = owner.state.history[work.acknowledged_command % 150];
    write(player, offsets.field_b5a66556, (current.state.flags >> 4U) & 7U);
    const float scaled_pitch = read<float>(player, offsets.field_66146802);
    const float captured_scaled = std::bit_cast<float>(owner.state.data_update.scaled_recoil);
    if (captured_scaled != scaled_pitch || *bindings.data_update_reset_marker != -1) {
        *bindings.data_update_reset_marker = -1;
        const auto scale = recoil_scale(bindings.recoil_scale_convar);
        if (scale != 0.0f) {
            if (auto* record = acknowledged_history(owner, work.acknowledged_command)) {
                const float pitch = scaled_pitch / scale;
                const float difference = record->recoil_angles[0] - pitch;
                if (!(std::fabs(difference) <= 0.000009999999747378752f)) {
                    if (quantize_shot_coordinate(pitch) !=
                        std::bit_cast<float>(work.original_recoil_pitch))
                        *bindings.data_update_reset_marker = work.acknowledged_command;
                    else {
                        write(player, offsets.vector_1bae1168, pitch);
                        record->recoil_angles[0] = pitch;
                        invalidate_command_prediction(bindings.prediction);
                    }
                }
            }
        }
    }

    const auto handle = read<std::uint32_t>(player, offsets.active_weapon);
    if (handle != UINT32_MAX) {
        if (auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, handle)) {
            const auto* identity = engine::invoke<const std::uint32_t*>(weapon, 2);
            current.state.weapon_identity = *identity;
            write(&current, 0x48, read<std::uint32_t>(weapon, offsets.weapon_field_6777c9ce));
        }
    }
    write(&current, 0x50, read<std::uint32_t>(player, offsets.field_66146802));
    write(&current, 0x4C, read<std::uint8_t>(player, offsets.field_bcb263a5));
}
} // namespace skeet::client
