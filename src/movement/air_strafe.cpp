#include "skeet/movement/air_strafe.hpp"

namespace skeet {

AirStrafeSelection select_air_strafe_modes(const AirStrafeSettings& settings, InputBitmap input,
                                           bool pre_speed_guard_enabled,
                                           const UserCommand& command) {
    const auto direction = static_cast<std::uint32_t>(settings.direction);
    AirStrafeSelection result{true, (direction & 1u) != 0, (direction >> 1) & 0xffffff01u, false,
                              false};
    result.pre_speed_requested =
        settings.pre_speed && hotkey_active(settings.pre_speed_hotkey, input);
    result.pre_speed_effective = result.pre_speed_requested && pre_speed_guard_enabled;
    const bool no_input = command.side_move == 0.0f && command.forward_move == 0.0f;
    if ((result.movement_key_mode & 0xffu) && !result.view_angles && !result.pre_speed_requested &&
        no_input) {
        result.continue_solver = false;
        return result;
    }
    if (no_input)
        result.movement_key_mode &= 0xffffff00u;
    return result;
}

} // namespace skeet
