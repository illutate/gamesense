#include "skeet/movement/command_state.hpp"
#include <limits>

namespace skeet {

MovementFeedbackHistory begin_movement_command(MovementFeedback& feedback,
                                               const UserCommand& command,
                                               std::uint32_t& original_buttons,
                                               std::uint32_t& scheduler_flags) noexcept {
    const MovementFeedbackHistory previous{feedback.primary_heading, feedback.turn_direction,
                                           feedback.secondary_heading};
    feedback.primary_heading = std::numeric_limits<float>::max();
    feedback.turn_direction = 0.0f;
    feedback.secondary_heading = std::numeric_limits<float>::max();
    feedback.original_buttons = command.buttons;
    scheduler_flags &= 0xfffd5f3cu;
    original_buttons = feedback.original_buttons;
    return previous;
}

} // namespace skeet
