#include "skeet/rage/targeting.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t convar_integer(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1c);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<std::int32_t>(field<std::uint32_t>(object, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
int compare_candidates(const void* left, const void* right) noexcept {
    const auto a = *static_cast<TargetCandidate* const*>(left);
    const auto b = *static_cast<TargetCandidate* const*>(right);
    return compare_rage_candidate_priorities(a->priority, b->priority);
}
}

bool target_is_enemy(const TargetBindings& bindings, const void* target) noexcept {
    const auto local = bindings.local_player;
    if (!local || !target || local == target)
        return false;
    if (convar_integer(bindings.game_type.get()) == 6) {
        const auto team = field<std::int32_t>(local, bindings.offsets.survival_team);
        return team == -1 || team != field<std::int32_t>(target, bindings.offsets.survival_team);
    }
    const auto team = field<std::int32_t>(local, bindings.offsets.team);
    const auto other_team = field<std::int32_t>(target, bindings.offsets.team);
    if (team == 2) {
        if (other_team == 3)
            return true;
    } else if (team == 3) {
        if (other_team == 2)
            return true;
    } else
        return false;
    return convar_integer(bindings.teammates_are_enemies.get()) != 0;
}

bool target_is_eligible(const TargetBindings& bindings, void* target) {
    if (!target || !engine::invoke<bool>(target, 158))
        return false;
    const auto& offsets = bindings.offsets;
    if (field<std::uint8_t>(target, offsets.rejection_bytes[0]) != 0)
        return false;
    if (engine::invoke<std::int32_t>(target, 122) <= 0)
        return false;
    const auto networkable = static_cast<std::byte*>(target) + 8;
    if (engine::invoke<bool>(networkable, 9))
        return false;
    if (field<std::uint8_t>(target, offsets.rejection_bytes[1]) != 0)
        return false;
    if (field<std::uint8_t>(target, offsets.rejection_bytes[2]) != 0)
        return false;
    const auto index = engine::invoke<std::int32_t>(networkable, 10);
    if (bindings.player_adjustments.records[std::uint32_t(index)].adjustments.whitelist != 0)
        return false;
    if (field<std::uint8_t>(target, offsets.rejection_bytes[3]) != 0)
        return false;
    return target_is_enemy(bindings, target);
}

RageVector target_eye_position(const TargetBindings& bindings, const void* target) noexcept {
    return target_eye_position(
        TargetEyePositionOffsets{bindings.offsets.origin, bindings.offsets.duck_amount}, target);
}
RageVector target_eye_position(TargetEyePositionOffsets offsets, const void* target) noexcept {
    auto result = field<RageVector>(target, offsets.origin);
    const auto duck = field<float>(target, offsets.duck_amount);
    const auto square = duck * duck;
    const auto cubic_twice = (square + square) * duck;
    const auto interpolation = 1.0f - (square * 3.0f - cubic_twice);
    result[2] = (interpolation * 18.0f + 46.0f) + result[2];
    return result;
}

bool target_passes_scan_filter(const TargetBindings& bindings, const void* target,
                               const TargetView& view) noexcept {
    const auto move_type = field<std::uint8_t>(target, bindings.offsets.movement_type);
    if (move_type == 8 || move_type == 10)
        return false;
    if (field<std::uint8_t>(target, bindings.offsets.movement_flags) & 0x40U)
        return false;
    if (view.field_of_view == 180)
        return true;
    const auto eye = target_eye_position(bindings, target);
    const auto x = eye[0] - view.origin[0];
    const auto y = eye[1] - view.origin[1];
    const auto z = eye[2] - view.origin[2];
    const auto length = std::sqrt((x * x + y * y) + z * z);
    const auto cosine =
        ((view.forward[1] * y + view.forward[0] * x) + view.forward[2] * z) / length;
    return !(view.minimum_cosine > cosine);
}

TargetBatch collect_target_candidates(std::span<TargetCandidate, 80> candidates,
                                      const std::array<std::uint32_t, 3>& completed,
                                      std::uint32_t context_flags,
                                      std::array<std::uint32_t, 2>& target_mask) noexcept {
    TargetBatch result;
    std::array<std::int8_t, 64> groups;
    groups.fill(-1);
    for (std::size_t i = 0; i != 80; ++i) {
        if (!(completed[i >> 5] & (1U << (i & 31))))
            continue;
        auto& candidate = candidates[i];
        const auto entity = candidate.entity_index & 63U;
        if (!(context_flags & 1U))
            target_mask[entity >> 5] |= 1U << (entity & 31);
        const auto slot = groups[entity];
        if (slot == -1) {
            if (result.count == result.candidates.size())
                continue;
            groups[entity] = std::int8_t(result.count);
            result.candidates[result.count++] = &candidate;
        } else if (candidate.priority > result.candidates[std::size_t(slot)]->priority)
            result.candidates[std::size_t(slot)] = &candidate;
    }
    return result;
}

void recalculate_target_priorities(TargetBatch& batch, const TargetBindings& bindings) {
    for (std::size_t i = 0; i != batch.count; ++i) {
        auto& candidate = *batch.candidates[i];
        bool taser = false;
        if (candidate.weapon_priority_enabled) {
            const auto entity =
                engine::invoke<void*>(bindings.entity_list, 3, candidate.entity_index);
            if (entity) {
                const auto handle = field<std::uint32_t>(entity, bindings.offsets.active_weapon);
                if (handle != 0xffffffffU) {
                    const auto weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
                    if (weapon)
                        taser =
                            field<std::uint16_t>(weapon, bindings.offsets.weapon_identifier) == 31;
                }
            }
        }
        RageCandidatePriority score;
        score.target_health = candidate.health;
        score.primary = {candidate.primary.damage, candidate.primary.hitgroup,
                         candidate.primary.flags, candidate.primary.chance,
                         candidate.primary.priority};
        score.prefer_body = candidate.prefer_body;
        score.prefer_safe = candidate.prefer_safe;
        score.target_weapon_priority_enabled = candidate.weapon_priority_enabled;
        score.target_weapon_is_taser = taser;
        score.target_priority =
            bindings.player_adjustments.records[candidate.entity_index].adjustments.high_priority !=
            0;
        score.target_state_flags = candidate.target_state_flags;
        candidate.priority = rage_candidate_priority(score);
    }
}

void sort_target_candidates(TargetBatch& batch) {
    std::qsort(batch.candidates.data(), batch.count, sizeof batch.candidates[0],
               compare_candidates);
}

TargetBatch rank_completed_target_candidates(std::span<TargetCandidate, 80> candidates,
                                             const std::array<std::uint32_t, 3>& completed,
                                             std::uint32_t context_flags,
                                             std::array<std::uint32_t, 2>& target_mask,
                                             const TargetBindings& bindings) {
    auto batch = collect_target_candidates(candidates, completed, context_flags, target_mask);
    recalculate_target_priorities(batch, bindings);
    if (batch.count > 1)
        sort_target_candidates(batch);
    return batch;
}

} // namespace skeet::rage
