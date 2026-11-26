#include "skeet/movement/command_runtime.hpp"
#include "skeet/movement/easy_strafe.hpp"

namespace skeet {

void process_movement_phase_one(MovementCommandOwner& owner, client::Command& command,
                                bool infinite_duck) noexcept {
    EasyStrafeState state{owner.previous_buttons_1e0, owner.suppressed_buttons_1e4,
                          owner.duck_peek.previous_buttons};
    process_easy_strafe(state, command.movement, owner.config.easy_strafe.value, infinite_duck,
                        owner.side_magnitude);
    owner.previous_buttons_1e0 = state.previous_buttons;
    owner.suppressed_buttons_1e4 = state.suppressed_buttons;
    owner.duck_peek.previous_buttons = state.saved_buttons;
}

} // namespace skeet
