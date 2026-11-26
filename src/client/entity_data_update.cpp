#include "skeet/client/entity_command_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/animation/entity_update.hpp"
#include "skeet/client/verified_command.hpp"
#include "skeet/network/packet_control.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <emmintrin.h>

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
using Vector = std::array<float, 3>;
const Command* command_at(const EntityCommandBindings& b, std::int32_t number) {
    const auto& command = read<const Command*>(b.input, 0xF0)[std::uint32_t(number) % 150u];
    return command.number == number ? &command : nullptr;
}
float quantized_angle(float value) noexcept { //full4337F9D3
    const auto integer =
        _mm_cvttss_si32(_mm_mul_ss(_mm_set_ss(value), _mm_set_ss(182.0444488525390625f)));
    return float(std::uint32_t(integer) & 0xFFFFu) * 0.0054931640625f;
}
bool near_vector(const Vector& actual, const Vector& expected, float tolerance) noexcept {
    for (unsigned i = 0; i < 3; ++i)
        if (!(std::fabs(actual[i] - expected[i]) <= tolerance))
            return false;
    return true;
}
// The two native diagnostic blocks43403B77..3FAF and43404CFD..5290
// compare the same history fields, with their supplied entry/tick identity.
std::uint32_t difference_flags(EntityCommandOwner& owner, void* player,
                               const prediction::CommandHistoryState& predicted,
                               std::int32_t number, bool compare_angles) {
    const auto& b = owner.bindings;
    const auto& o = b.offsets;
    std::uint32_t flags =
        read<std::int32_t>(&predicted) != read<std::int32_t>(player, o.tick_base) ? 1u : 0u;
    if (!near_vector(read<Vector>(player, o.vector_5ae32976), read<Vector>(&predicted, 4),
                     0.03125f))
        flags |= 2u;
    if (!near_vector(read<Vector>(player, o.vector_1bae1168), predicted.recoil_angles, 0.03125f))
        flags |= 4u;
    if (!near_vector(read<Vector>(player, o.vector_48442721), read<Vector>(&predicted, 0x1C),
                     0.03125f))
        flags |= 8u;
    if (!(std::fabs(read<float>(player, o.view_offset + 8) - read<float>(&predicted, 0x28)) <=
          0.25f))
        flags |= 0x10u;
    const auto position = read<Vector>(player, o.network_origin);
    const auto prior = read<Vector>(&predicted, 0x2C);
    const auto x = position[0] - prior[0], y = position[1] - prior[1], z = position[2] - prior[2];
    if ((y * y + x * x) + z * z >= 0.0625f)
        flags |= 0x20u;
    if (read<std::int32_t>(player, o.shots_fired_8cfc633a) != read<std::int32_t>(&predicted, 0x3C))
        flags |= 0x40u;
    if (!(std::fabs(read<float>(player, o.field_66146802) - read<float>(&predicted, 0x4C)) <=
          0.000099999997473787516f))
        flags |= 0x400u;
    const auto handle = read<std::uint32_t>(player, o.active_weapon);
    if (predicted.weapon_identity != handle)
        flags |= 0x80u;
    else if (handle != UINT32_MAX)
        if (auto* weapon = engine::invoke<void*>(b.entity_list, 4, handle)) {
            if (!(std::fabs(read<float>(weapon, o.weapon_field_d2563274) -
                            read<float>(&predicted, 0x64)) <= 0.000099999997473787516f))
                flags |= 0x100u;
            if (read<float>(&predicted, 0x60) != read<float>(weapon, o.weapon_field_0176a381))
                flags |= 0x200u;
            if (!(std::fabs(read<float>(weapon, o.weapon_field_6777c9ce) -
                            read<float>(&predicted, 0x44)) <= 0.000099999997473787516f))
                flags |= 0x1000u;
            auto* model = entity_command_view_model(
                owner, player, read<std::int32_t>(weapon, o.weapon_viewmodel_index_c6c78c82));
            if (model && read<std::uint32_t>(&predicted, 0x74) !=
                             read<std::uint32_t>(model, o.viewmodel_695ff325))
                flags |= 0x2000u;
        }
    if (const auto* command = command_at(b, number)) {
        const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
        auto* cached = b.players->get(index);
        if (compare_angles) {
            auto angles = command->angles;
            b.angle_history.select(number, angles);
            const auto yaw = quantized_angle(angles[1]);
            const auto pitch = quantized_angle(angles[0]);
            if (pitch != read<float>(&cached->native, 0x1A8) || yaw != cached->native.reference_yaw)
                flags |= 0x800u;
        }
    }
    return flags;
}
void repair_weapon_activity(EntityCommandOwner& owner, void* player) {
    const auto& b = owner.bindings;
    const auto& o = b.offsets;
    const auto handle = read<std::uint32_t>(player, o.active_weapon);
    if (handle == UINT32_MAX)
        return;
    auto* weapon = engine::invoke<void*>(b.entity_list, 4, handle);
    if (!weapon || read<std::int32_t>(weapon, o.weapon_field_01aa449d) != 2)
        return;
    auto* viewmodel = entity_command_view_model(
        owner, player, read<std::int32_t>(weapon, o.weapon_viewmodel_index_c6c78c82));
    if (!viewmodel)
        return;
    const auto model_weapon = read<std::uint32_t>(viewmodel, o.viewmodel_weapon_a116369a);
    if (model_weapon == UINT32_MAX)
        return;
    auto* resolved = engine::invoke<void*>(b.entity_list, 4, model_weapon);
    if (resolved != weapon)
        return;
    const auto sequence = read<std::int32_t>(viewmodel, o.viewmodel_sequence_a25d8271);
    if (sequence < 0)
        return;
    const auto activity = b.sequence_activity ? b.sequence_activity(viewmodel, sequence) : -1;
    if (activity == 0xB7 || activity == 0x1FA)
        write(weapon, o.weapon_state_9e2078a1, activity);
}
void reset_prediction_history(EntityCommandOwner& owner) noexcept {
    for (unsigned i = 0; i < 150; ++i)
        owner.state.revolver[i].command_number = -1;
    for (unsigned i = 0; i < 150; ++i)
        owner.state.history[i].command_number = -1;
    auto& saved = owner.state.data_update;
    saved.recoil_angles = {0, 0, 0};
    saved.scaled_recoil = 0;
    saved.field_7f5ed2d0 = 0x3F800000u;
    owner.state.last_choked_shot = -1;
    *owner.bindings.data_update_reset_marker = -1;
}
void record_recoil_mismatch(EntityCommandOwner& owner, void* player) {
    const auto& b = owner.bindings;
    const auto& o = b.offsets;
    *b.data_update_reset_marker = engine::invoke<std::int32_t>(b.engine_client, 205);
    if (!(read<std::uint8_t>(player, o.ground_flags) & 1u))
        return;
    const auto handle = read<std::uint32_t>(player, o.active_weapon);
    if (handle == UINT32_MAX)
        return;
    auto* weapon = engine::invoke<void*>(b.entity_list, 4, handle);
    if (weapon && network::classified_packet_weapon(
                      read<std::uint16_t>(weapon, o.weapon_definition), b.weapon_classification))
        *b.scheduler_flags = (*b.scheduler_flags & ~0x400u) | 0x200u;
}
void merge_acknowledged_record(EntityCommandOwner& owner, void* player,
                               prediction::CommandHistoryEntry& entry, std::uint32_t flags,
                               std::int32_t number) {
    const auto& b = owner.bindings;
    const auto& o = b.offsets;
    if (flags & 1u) {
        if (flags != 1u)
            return;
        if (!(std::fabs(*b.prediction_time - std::bit_cast<float>(owner.state.prediction_stamp)) <=
              0.2f) &&
            !(entry.state.flags & 8u))
            return;
    }
    const auto merge_vector = [&](std::ptrdiff_t record_offset, std::ptrdiff_t player_offset,
                                  std::uint32_t mismatch) {
        if (flags & mismatch)
            write(&entry.state, record_offset, read<Vector>(player, player_offset));
        else
            write(player, player_offset, read<Vector>(&entry.state, record_offset));
    };
    merge_vector(4, o.vector_5ae32976, 2u);
    merge_vector(0x10, o.vector_1bae1168, 4u);
    merge_vector(0x1C, o.vector_48442721, 8u);
    if (flags & 0x10u)
        write(&entry.state, 0x28, read<std::uint32_t>(player, o.view_offset + 8));
    else
        write(player, o.view_offset + 8, read<std::uint32_t>(&entry.state, 0x28));
    if (flags & 0x20u)
        write(&entry.state, 0x2C, read<Vector>(player, o.network_origin));
    else {
        write(player, o.field_fb7e3404 + 4, read<std::uint32_t>(&entry.state, 0x50));
        write(player, o.field_fb7e3404 + 8, read<std::uint32_t>(&entry.state, 0x54));
    }
    if (flags & 0x400u)
        write(&entry.state, 0x4C, read<std::uint32_t>(player, o.field_66146802));
    const auto scalar = read<float>(&entry.state, 0x38);
    if (read<float>(player, o.scalar_2f33c681) == quantize_entity_prediction_scalar(scalar))
        write(player, o.scalar_2f33c681, scalar);
    else
        write(&entry.state, 0x38, read<std::uint32_t>(player, o.scalar_2f33c681));
    if (flags & 0x34u) {
        const auto last = *b.last_outgoing;
        for (unsigned i = 0; i < 150 && number <= last; ++i) {
            auto& current = owner.state.history[number % 150];
            if (current.command_number != number)
                break;
            current.state.flags |= 2u;
            number = std::bit_cast<std::int32_t>(std::uint32_t(number) + 1u);
        }
        invalidate_command_prediction(b.prediction);
    }
}
}
void reset_entity_prediction_history(EntityCommandOwner& owner) noexcept {
    reset_prediction_history(owner);
}
void process_entity_data_update_capture(EntityCommandOwner& owner, void* player, std::int32_t) {
    const auto& offsets = owner.bindings.offsets;
    auto& saved = owner.state.data_update;
    saved.field_7f5ed2d0 = read<std::uint32_t>(player, offsets.field_7f5ed2d0);
    for (unsigned i = 0; i < 3; ++i)
        saved.recoil_angles[i] = read<std::uint32_t>(player, offsets.vector_1bae1168 + i * 4);
    saved.scaled_recoil = read<std::uint32_t>(player, offsets.field_66146802);
    saved.tick_base = read<std::int32_t>(player, offsets.tick_base);
    saved.health = engine::invoke<std::int32_t>(player, 122);
    saved.field_47a6f50c = read<std::uint32_t>(player, offsets.field_47a6f50c);
}
std::uint32_t entity_data_update_difference_flags(EntityCommandOwner& owner, void* player,
                                                  const prediction::CommandHistoryState& predicted,
                                                  std::int32_t number, bool compare_angles) {
    return difference_flags(owner, player, predicted, number, compare_angles);
}
float entity_data_update_quantized_angle(float angle) noexcept {
    return quantized_angle(angle);
}
void process_entity_data_update_finish(EntityCommandOwner& owner, void* player,
                                       std::int32_t update) {
    const auto& b = owner.bindings;
    const auto& o = b.offsets;
    repair_weapon_activity(owner, player);
    if (update == 0 && *b.data_update_backup_mode != 2) {
        reset_entity_prediction_history(owner);
        return;
    }
    if (read<std::uint8_t>(player, o.rejected_9d920e35))
        return;
    const auto acknowledged = engine::invoke<std::int32_t>(b.engine_client, 205);
    const auto delta = std::bit_cast<std::int32_t>(std::uint32_t(acknowledged) -
                                                   std::uint32_t(*b.old_acknowledged));
    const auto original_recoil = read<std::uint32_t>(player, o.vector_1bae1168);
    const auto captured_tick = owner.state.data_update.tick_base;
    const auto current_tick = read<std::int32_t>(player, o.tick_base);
    const bool changed = std::bit_cast<float>(owner.state.data_update.field_7f5ed2d0) !=
                         read<float>(player, o.field_7f5ed2d0);
    EntityDataUpdateWork work{acknowledged, delta,           captured_tick,
                              current_tick, original_recoil, changed};
    reconcile_entity_data_update_ack(owner, player, work);
    auto& entry = owner.state.history[acknowledged % 150];
    work.current_record = &entry;
    if (entry.command_number == acknowledged) {
        const auto flags = difference_flags(owner, player, entry.state, acknowledged, true);
        if (flags & 4u)
            record_recoil_mismatch(owner, player);
        merge_acknowledged_record(owner, player, entry, flags, acknowledged);
    }
    finish_entity_data_update_tail(owner, player, work);
}
} //namespace skeet::client
