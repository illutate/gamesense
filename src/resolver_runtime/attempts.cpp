#include "skeet/resolver_runtime/attempts.hpp"

namespace skeet::resolver_runtime {

std::uint8_t classify_winning_pose(float pose_yaw) noexcept {
    if (pose_yaw < -20.0f)
        return 2;
    return pose_yaw > 20.0f ? 1 : 0;
}

void advance_pending_attempt(ResolverRecord& record, std::uint32_t options) noexcept {
    if ((options & 4U) != 0 && (record.state_flags & 0xc0U) == 0xc0U) {
        record.state_flags &= ~0x40U;
        select_resolver_variant(record, static_cast<std::uint8_t>(record.selected_attempt + 1U));
    }
}

std::uint32_t propagate_attempt(RecordHistory& history, std::uint8_t mode,
                                std::uint8_t previous_attempt, std::uint8_t new_attempt) noexcept {
    if (previous_attempt == new_attempt)
        return 0;
    std::uint32_t processed = 0;
    for (std::uint32_t index = 0; index != history.count; ++index) {
        auto& record = *history.records[(history.first + index) & 31U];
        if ((record.state_flags & 0x22U) != 0 || record.variant_mode != mode ||
            record.selected_attempt != previous_attempt)
            break;
        select_resolver_variant(record, new_attempt);
        record.state_flags = (record.state_flags & ~0x40U) | 2U;
        ++processed;
        if ((record.state_flags & 0x200U) != 0)
            break;
    }
    return processed;
}

void apply_feedback(RecordHistory& history, ResolverRecord& source, std::uint8_t previous_attempt,
                    std::uint8_t new_attempt) noexcept {
    propagate_attempt(history, source.variant_mode, previous_attempt, new_attempt);
    source.state_flags |= 2U;
}

void publish_feedback(SelectionState& state, RecordHistory& history, ResolverRecord& source,
                      std::uint8_t previous_attempt, std::uint8_t new_attempt) noexcept {
    state.attempt_by_mode[source.variant_mode] = new_attempt;
    state.status = 1;
    if (source.variant_mode == 1)
        state.attempt_by_mode[2] = new_attempt;
    else if (source.variant_mode == 2)
        state.attempt_by_mode[1] = new_attempt;
    apply_feedback(history, source, previous_attempt, new_attempt);
}

} // namespace skeet::resolver_runtime
