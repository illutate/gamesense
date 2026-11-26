#include "skeet/movement/easy_strafe.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

// 0x4333F746..0x4333F8E2, secondary interface receives owner + 8.
void process_easy_strafe(EasyStrafeState& state, UserCommand& command, bool enabled,
                         bool force_bullrush, float default_move) noexcept {
    const auto original = command.buttons;
    if (force_bullrush)
        command.buttons |= 0x400000;
    if (enabled) {
        const auto current = command.buttons & 0x618u;
        const auto pressed = (state.previous_buttons ^ current) & current;
        auto suppressed = state.suppressed_buttons;
        const auto update = [&](std::uint32_t key, std::uint32_t opposite) {
            if (pressed & key)
                suppressed = (suppressed & ~key) | opposite;
            else if (!(current & key))
                suppressed &= ~opposite;
        };
        update(move_left_button, move_right_button);
        update(move_right_button, move_left_button);
        update(forward_button, back_button);
        update(back_button, forward_button);
        state.suppressed_buttons = suppressed;
        command.buttons &= ~suppressed;
        if ((command.buttons & 0x600) != 0x600) {
            if (command.buttons & move_left_button) {
                float magnitude = absolute(command.side_move);
                if (magnitude == 0.0f)
                    magnitude = default_move;
                command.side_move = negate(magnitude);
            } else if (command.buttons & move_right_button) {
                float magnitude = absolute(command.side_move);
                command.side_move = magnitude == 0.0f ? default_move : magnitude;
            }
        }
        if ((command.buttons & 0x18) != 0x18) {
            if (command.buttons & forward_button) {
                float magnitude = absolute(command.forward_move);
                command.forward_move = magnitude == 0.0f ? default_move : magnitude;
            } else if (command.buttons & back_button) {
                float magnitude = absolute(command.forward_move);
                if (magnitude == 0.0f)
                    magnitude = default_move;
                command.forward_move = negate(magnitude);
            }
        }
    } else {
        state.suppressed_buttons = 0;
    }
    state.previous_buttons = original;
    state.saved_buttons = original;
}

} // namespace skeet
