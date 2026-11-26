#include "skeet/core.hpp"

namespace skeet {
// Complete callback 0x433D1F99..0x433D1FF4, including its shared tail.
void post_prediction(const MovementSettings& settings, MovementFrame& frame, UserCommand& command,
                     const HotkeyBinding& binding, InputBitmap input) {
    if (settings.jump_at_edge && hotkey_active(binding, input)) {
        const bool held = (command.buttons & jump_button) != 0;
        const bool was_grounded = (frame.saved_player_flags & on_ground) != 0;
        const bool now_grounded = (frame.packed_flags & frame_on_ground) != 0;
        if (!held && was_grounded && !now_grounded)
            command.buttons |= jump_button;
    }
    if ((settings.shared_callback_flags & 1) != 0)
        frame.packed_flags |= frame_shared_flag;
}
} // namespace skeet
