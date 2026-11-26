#include "skeet/rage/targeting.hpp"
#include <vector>

namespace skeet::rage {
void select_target_points(TargetCandidate& candidate, std::span<const TargetPoint> points,
                          std::uint32_t priority_mask) {
    std::vector<RagePointScore> scores;
    scores.reserve(points.size());
    for (const auto& point : points)
        scores.push_back({point.damage, point.hitgroup, point.flags, point.chance, point.priority});
    const auto selection = select_rage_points(scores, priority_mask, candidate.prefer_body,
                                              candidate.prefer_safe, candidate.flags);
    candidate.flags = selection.candidate_flags;
    if (selection.primary != no_rage_point)
        candidate.primary = points[selection.primary];
    if (selection.alternate_first != no_rage_point)
        candidate.alternate_first = points[selection.alternate_first];
    if (selection.alternate_second != no_rage_point)
        candidate.alternate_second = points[selection.alternate_second];
}

bool prepare_target_record_policy(TargetRecordPolicy& policy, std::uint32_t record_flags,
                                  std::uint32_t ordinal, std::uint8_t body_disablers) noexcept {
    if (record_flags & 0x40U) {
        if (ordinal)
            return false;
        policy.evaluation_flags |= 0x20U;
        policy.body.context_ready = true;
    }
    if (record_flags & 0x10U)
        policy.evaluation_flags |= 8U;
    if (record_flags & 0x20U)
        policy.evaluation_flags |= 0x10U;
    apply_rage_body_disablers(policy.body, body_disablers);
    policy.body.hitboxes.safe &= ~0x200U;
    if ((record_flags & 0x70U) || (policy.body.target_state_flags & 0x20U))
        policy.delay_shot = false;
    return true;
}
} // namespace skeet::rage
