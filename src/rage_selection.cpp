#include "skeet/rage_selection.hpp"
#include "rage_math.hpp"
#include <cmath>

namespace skeet {
using namespace rage_detail;

bool admit_rage_point(RagePointScore& point, const RagePointRequirements& requirements,
                      RagePointAdmissionState& state,
                      const std::array<float, 3>& descriptor_position,
                      std::uint32_t descriptor_flags) noexcept {
    if (!(descriptor_flags & 2U))
        state.reference = {descriptor_position, point.damage};
    if (point.damage == 0)
        return false;
    if (point.damage < requirements.minimum_damage) {
        point.flags |= 0x80U;
        return false;
    }
    if (!(point.chance >= requirements.minimum_chance)) {
        if (!(state.evaluation_flags & 9U))
            state.evaluation_flags |= 0x100U;
        point.flags |= 0x80U;
        return false;
    }
    if (requirements.prefer_body && (state.priority_mask & 0x100000U) && point.hitgroup == 1 &&
        (point.flags & 1U) && (state.body_disabler_reasons & 8U))
        state.priority_mask &= ~0x100000U;
    return true;
}

std::uint32_t rage_point_priority(const RagePointScore& point,
                                  const RagePointRequirements& requirements) noexcept {
    auto result = damage_priority(requirements.target_health, point.damage);
    if (!(point.flags & 2U))
        result |= 0x80U;
    result |= chance_priority(point.chance) << 8;
    if (point.flags & 1U)
        result |= 0x8000U;
    const auto shots = shots_to_kill(requirements.target_health, point.damage);
    const auto inverse_shots = add(15, -shots);
    result |= std::uint32_t(std::clamp(inverse_shots, 0, 15)) << 16;
    if (requirements.prefer_body && (point.hitgroup == 2 || point.hitgroup == 3))
        result |= 0x100000U;
    if (point.chance >= requirements.minimum_chance)
        result |= 0x200000U;
    if (point.damage >= requirements.minimum_damage)
        result |= 0x400000U;
    return result;
}

RagePointSelection select_rage_points(std::span<const RagePointScore> points,
                                      std::uint32_t priority_mask, bool prefer_body,
                                      bool prefer_safe, std::uint32_t candidate_flags) noexcept {
    RagePointSelection result;
    result.candidate_flags = candidate_flags;
    if (points.empty())
        return result;
    result.primary = 0;
    for (std::size_t i = 1; i < points.size(); ++i)
        if ((points[i].priority & priority_mask) >
            (points[result.primary].priority & priority_mask))
            result.primary = i;
    std::size_t body = no_rage_point;
    if (prefer_body && points[result.primary].hitgroup == 1) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (points[i].hitgroup != 2 && points[i].hitgroup != 3)
                continue;
            if (body == no_rage_point || points[i].priority > points[body].priority)
                body = i;
        }
        if (body != no_rage_point) {
            result.alternate_first = body;
            result.candidate_flags |= 8U;
        }
    }
    if (prefer_safe && (points[result.primary].flags & 1U)) {
        std::size_t regular = no_rage_point;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (points[i].flags & 1U)
                continue;
            if (regular == no_rage_point || points[i].priority > points[regular].priority)
                regular = i;
        }
        if (regular != no_rage_point && regular != body) {
            if (result.candidate_flags & 8U) {
                result.alternate_second = regular;
                result.candidate_flags |= 0x10U;
            } else {
                result.alternate_first = regular;
                result.candidate_flags |= 8U;
            }
        }
    }
    return result;
}

std::uint32_t rage_candidate_priority(const RageCandidatePriority& candidate) noexcept {
    const auto& point = candidate.primary;
    auto result = damage_priority(candidate.target_health, point.damage) |
                  (chance_priority(point.chance) << 9);
    if (candidate.prefer_body && (point.hitgroup == 2 || point.hitgroup == 3))
        result |= 0x100U;
    if (candidate.target_weapon_priority_enabled && !candidate.target_weapon_is_taser)
        result |= 0x80U;
    if (candidate.prefer_safe && (point.flags & 1U))
        result |= 0x10000U;
    if (candidate.target_priority)
        result |= 0x20000U;
    const auto shots = std::clamp(shots_to_kill(candidate.target_health, point.damage), 0, 15);
    result |= ((~std::uint32_t(shots)) & 15U) << 19;
    if (candidate.target_state_flags & 0x1800U)
        result |= 0x40000U;
    return result;
}

std::int32_t compare_rage_candidate_priorities(std::uint32_t left, std::uint32_t right) noexcept {
    return signed_word(right - left);
}

} // namespace skeet
