#include "skeet/rage/acquisition.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
const lagcomp::EvaluationState& synchronize_evaluation_state(const TargetCandidate& candidate,
                                                             AcquisitionEvaluation& slot) noexcept {
    auto& words = slot.native.words;
    words[0] = candidate.flags;
    words[1] = candidate.entity_index;
    words[2] = std::uint32_t(candidate.health);
    std::memcpy(words.data() + 3, &candidate.primary, sizeof(TargetPoint));
    std::memcpy(words.data() + 17, &candidate.alternate_first, sizeof(TargetPoint));
    std::memcpy(words.data() + 31, &candidate.alternate_second, sizeof(TargetPoint));
    words[45] = std::uint32_t(slot.requested_tick);
    words[46] = slot.body_disabler_reasons;
    std::memcpy(words.data() + 47, slot.settings.words.data(), sizeof(slot.settings.words));
    // DC/DD retain their native stored bytes; the bool views do not turn
    // noncanonical true values into1 during publication.
    words[57] = candidate.priority;
    words[58] = slot.ballistic_evaluations;
    return slot.native;
}
void restore_evaluation_state(const lagcomp::EvaluationState& input, TargetCandidate& candidate,
                              AcquisitionEvaluation& slot) noexcept {
    if (&input != &slot.native)
        std::memcpy(&slot.native, &input, sizeof(input));
    const auto& words = slot.native.words;
    candidate.flags = words[0];
    candidate.entity_index = words[1];
    candidate.health = std::bit_cast<std::int32_t>(words[2]);
    std::memcpy(&candidate.primary, words.data() + 3, sizeof(TargetPoint));
    std::memcpy(&candidate.alternate_first, words.data() + 17, sizeof(TargetPoint));
    std::memcpy(&candidate.alternate_second, words.data() + 31, sizeof(TargetPoint));
    slot.requested_tick = std::bit_cast<std::int32_t>(words[45]);
    slot.body_disabler_reasons = words[46];
    std::memcpy(slot.settings.words.data(), words.data() + 47, sizeof(slot.settings.words));
    candidate.priority = words[57];
    slot.ballistic_evaluations = words[58];
    candidate.prefer_safe = slot.settings.option(0xDC) != 0;
    candidate.prefer_body = slot.settings.option(0xDD) != 0;
    candidate.weapon_priority_enabled = (slot.record.header.player_flags & 1U) != 0;
    candidate.target_state_flags = slot.record.header.flags;
    slot.source_record =
        reinterpret_cast<const lagcomp::LagRecord*>(std::uintptr_t(slot.record.header.field_00));
}
} // namespace skeet::rage
