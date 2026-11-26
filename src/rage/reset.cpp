#include "skeet/rage/reset.hpp"
#include "skeet/rage/command_owner.hpp"
#include "skeet/rage/post_firing.hpp"
#include "skeet/rage/command_phase6.hpp"

namespace skeet::rage {
void reset_rage_state(const RageResetBindings& bindings) noexcept { //4340BCDA
    bindings.field_174 = -1;
    bindings.highlight.target_handle = -1;
    bindings.shot_index = 0;
    bindings.state.target_mask.fill(0);
    bindings.state.previous_targets.fill(0);
    bindings.state.completed.fill(0);
    bindings.state.spread.generation = UINT32_MAX;
    bindings.acquisition.queued.clear();
    bindings.state.evaluation_count = 0;
    bindings.state.frame_count = 0;
    bindings.state.frame_first = 0;
    publish_command_animation_angles(bindings.pending_angles, -1, {0.0f, 0.0f, 0.0f});
}

void reset_rage_command_owner(CommandOwner& owner, std::int32_t& field_174) noexcept {
    reset_rage_state({owner.state, owner.acquisition, owner.post_firing.shot_index, field_174,
                      owner.tail.action_state, owner.phase6.pending_angles});
}
}
