#include "skeet/rage/post_firing.hpp"
#include "skeet/rage/prediction_mask_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include "../rage_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace skeet::rage {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
void* weapon(const client::CommandFrame& frame) noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
}
bool quick_stop_key(const PostFiringMovementBindings& b) {
    return b.settings.quick_stop.value &&
           hotkey_active(b.settings.quick_stop_key.value.binding, b.input);
}
std::uint32_t options(const client::CommandFrame& frame,
                      const PostFiringMovementBindings& b) noexcept {
    auto result = std::uint32_t(b.settings.quick_stop_options.value);
    if (frame.field_1d4 == 1)
        result |= std::uint32_t(b.settings.double_tap_quick_stop.value);
    return result;
}
bool publish_stop(const client::CommandFrame& frame, std::uint32_t policy,
                  const PostFiringMovementBindings& b) {
    if (frame.weapon_id_d8 == 31 && !(policy & 0x40U))
        return false;
    if ((policy & 8U) && !(frame.flags & 0x400U)) {
        if (frame.weapon_address && engine::invoke<float>(weapon(frame), 433, 0) > 1.0f)
            return false;
        if (read<float>(b.targeting.local_player, b.offsets.player_next_attack) - b.current_time >
            0.4f)
            return false;
    }
    b.scheduler_flags |= 8U;
    return true;
}
void damage_stop(const client::CommandFrame& frame, const TargetCandidate* leading,
                 const AcquisitionEvaluation* evaluation, const PostFiringMovementBindings& b) {
    if (!leading)
        return;
    const auto origin =
        RageVector{frame.origin_10c[0], frame.origin_10c[1], frame.origin_10c[2] + 46.0f};
    auto query = prepare_target_bullet(weapon(frame), b.weapon_item,
                                       b.settings.automatic_penetration.value != 0);
    anti_aim::TargetTraceFilter native_filter(
        b.trace_filter_vtable, b.trace_entities, 0x600400BU, 0x600400BU,
        float(std::bit_cast<std::int32_t>(evaluation->settings.words[5])) /
            rage_detail::hitgroup_multipliers[std::uint32_t(leading->primary.hitgroup) & 15U]);
    trace_ballistic_bullet(b.ballistics, b.source, origin, leading->primary.position,
                           native_filter.binding(), query);
    auto damage = _mm_cvtt_ss2si(_mm_set_ss(query.damage));
    if (damage) {
        auto* target = engine::invoke<void*>(b.targeting.entity_list, 3, leading->entity_index);
        if (target) {
            const auto info =
                b.ballistics.entity_info(BallisticEntity(reinterpret_cast<std::uintptr_t>(target)));
            damage = final_bullet_damage(
                query.damage, query.armor_ratio, std::bit_cast<std::int32_t>(query.reserved),
                query.weapon_id, leading->primary.hitgroup, info.armor, info.helmet);
        }
    }
    if (damage >= std::bit_cast<std::int32_t>(evaluation->settings.words[5]))
        b.scheduler_flags |= 0x11000U;
}
void between_shots(const client::CommandFrame& frame, const PostFiringMovementBindings& b) {
    if (!(b.context_flags & 0x40U) || !(frame.flags & 0x40000U))
        return;
    const auto time = (frame.player_flags_7c & 2U)
                          ? engine::invoke<float>(weapon(frame), 436, frame.field_b0)
                          : engine::invoke<float>(weapon(frame), 435, frame.field_b0);
    const auto difference = std::bit_cast<float>(
        std::bit_cast<std::uint32_t>(frame.inaccuracy_c4 - time) & 0x7FFFFFFFU);
    if (0.0001f > difference)
        b.scheduler_flags |= 0x10U;
}
}
bool post_firing_prediction_allowed(const PostFiringPredictionMasks& source) noexcept {
    const auto& masks = prediction_mask_state(*source.owner);
    if (masks.blocked[0] || masks.blocked[1])
        return false;
    return masks.allowed[0] || masks.allowed[1];
}
std::size_t select_post_firing_candidate(const PreambleState& state,
                                         const AcquisitionStorage& storage,
                                         std::int32_t remembered) noexcept {
    auto select = [&](bool match) {
        auto best = no_rage_point;
        for (std::size_t i = 0; i < state.evaluation_count; ++i) {
            if (match &&
                storage.candidates[i].entity_index != (std::uint32_t(remembered) & 0xFFFFU))
                continue;
            if (best == no_rage_point ||
                std::bit_cast<std::int32_t>(storage.evaluations[i].ballistic_evaluations) >
                    std::bit_cast<std::int32_t>(storage.evaluations[best].ballistic_evaluations))
                best = i;
        }
        return best;
    };
    const auto matching = remembered != -1 ? select(true) : no_rage_point;
    return matching != no_rage_point ? matching : select(false);
}
void finish_post_firing_movement(client::CommandFrame& frame, client::Command& command,
                                 const TargetCandidate* leading,
                                 const AcquisitionEvaluation* evaluation, bool ground_gate,
                                 bool restore_bit, const PostFiringMovementBindings& b) {
    if (b.settings.automatic_scope.value && !(frame.flags & 0x4000U) &&
        (b.context_flags & 0x100U)) {
        const bool grounded =
            (frame.flags & 0x40000U) &&
            (std::to_integer<std::uint8_t>(frame.fields_e8[0x10]) & std::uint32_t(ground_gate));
        const auto speed = frame.velocity_150[0] * frame.velocity_150[0] +
                           frame.velocity_150[1] * frame.velocity_150[1];
        if (grounded || (frame.weapon_id_d8 == 40 &&
                         !(frame.previous_command.movement.buttons & 0x618U) && 900.0f > speed)) {
            if (engine::invoke<std::int32_t>(weapon(frame), 443) > 0 &&
                !read<std::uint8_t>(b.targeting.local_player, b.offsets.scope_guard_cacaeecc) &&
                !read<std::uint8_t>(b.targeting.local_player, b.offsets.scope_guard_c07d1150) &&
                !(command.movement.buttons & 1U))
                command.movement.buttons |= 0x800U;
        }
    }
    if ((b.context_flags & 0x80U) && quick_stop_key(b) && frame.state_104 == 2) {
        const auto policy = options(frame, b);
        if (publish_stop(frame, policy, b) && leading) {
            if (policy & 2U)
                damage_stop(frame, leading, evaluation, b);
            between_shots(frame, b);
        }
    }
    if ((std::uint32_t(b.settings.quick_stop_options.value) & 0x20U) &&
        lagcomp::history_weapon_is_firearm(std::uint16_t(frame.weapon_id_d8)) &&
        frame.weapon_id_d8 != 31 && frame.state_104 == 2 && (frame.flags & 0x40000U) &&
        !(frame.previous_command.movement.buttons & 6U) && b.remembered_handle != -1) {
        auto* target = engine::invoke<void*>(b.targeting.entity_list, 4, b.remembered_handle);
        if (target && !engine::invoke<bool>(static_cast<std::byte*>(target) + 8, 9) &&
            post_firing_prediction_allowed(b.prediction))
            b.scheduler_flags |= 0x100U;
    }
    if ((frame.flags & 0x20000U) && !(b.scheduler_flags & 8U) && (b.scheduler_flags & 0x4000U)) {
        if ((b.target_mask[0] || b.target_mask[1]) && frame.state_104 == 2) {
            const auto policy = options(frame, b);
            if (publish_stop(frame, policy, b) && leading) {
                if (policy & 2U)
                    damage_stop(frame, leading, evaluation, b);
                between_shots(frame, b);
            }
        }
    }
    frame.flags = (frame.flags & ~0x400U) | (restore_bit ? 0x400U : 0U);
}
} // namespace skeet::rage
