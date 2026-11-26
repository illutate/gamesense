#include "skeet/rage_shot.hpp"

namespace skeet {
RageShotCommit commit_accepted_rage_shot(RageShotState& state, RageShotCommitState& shared,
                                         UserCommand& command, const RageShotCandidate& candidate,
                                         float special_gate_value, bool alternate_path,
                                         bool send_mode) noexcept {
    command.buttons |= state.flags & 0x800U ? 0x800U : 1U;
    if (state.weapon_id == 0x40 && !(state.flags & 0x800U) &&
        !(special_gate_value > state.special_gate_threshold))
        return RageShotCommit::attack_button_only;
    if (alternate_path)
        state.flags |= 0x20000000U;
    if (candidate.flags & 4U)
        state.flags |= 0x02000000U;
    shared.context_flags |= 0x800U;
    state.flags |= 0x1000U;
    state.selected_aim_point = candidate.aim_point;
    shared.last_committed_marker = state.marker;
    if (shared.send_packet == 0 && send_mode)
        shared.send_packet = 1;
    shared.command_flags = (shared.command_flags & ~0x400U) | 0x200U;
    return RageShotCommit::recorded;
}
} // namespace skeet
