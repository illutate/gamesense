#include "skeet/rage/feedback_command_owner.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
void process_feedback_command_phase4(FeedbackCommandOwner& owner,
                                     const client::CommandFrame& frame) noexcept {
    if (!frame.choked_15c)
        return;
    for (auto& record : owner.feedback.pending) {
        const auto first = std::bit_cast<std::int32_t>(record.words[1]);
        if (first > owner.last_outgoing_command && first < frame.command_number_ac)
            record.words[3] = std::uint32_t(frame.command_number_ac);
    }
}

void process_feedback_command_phase6(FeedbackCommandOwner& owner,
                                     const client::CommandFrame& frame) noexcept {
    if (!owner.send_gate || owner.feedback.pending.empty())
        return;
    auto& record = owner.feedback.pending.back();
    if (record.words[1] != std::uint32_t(frame.command_number_ac))
        return;
    std::memcpy(record.words.data() + 9, frame.origin_dc.data(), sizeof(frame.origin_dc));
}
} // namespace skeet::rage
