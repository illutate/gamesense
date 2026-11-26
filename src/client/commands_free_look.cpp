#include "skeet/client/commands.hpp"
#include "skeet/movement/steering_math.hpp"
#include <cmath>

namespace skeet::client {

void set_command_view_angles(const CommandAngleBindings& bindings, std::array<float, 3>& angles) {
    for (float& angle : angles)
        if (std::isnan(angle))
            angle = 0.0f;
    if (!command_angles_unrestricted(bindings.environment)) {
        MovementAngles value{angles[0], angles[1], angles[2]};
        normalize_movement_angles(value);
        angles = {value.pitch, value.yaw, value.roll};
    }
    angles[2] = 0.0f;
    engine::invoke<void>(bindings.engine_client, 19, angles.data());
}

void process_command_free_look(FreeLookState& state, Command& command, const HotkeyBinding& hotkey,
                               InputBitmap keys, const CommandAngleBindings& bindings) {
    if (!hotkey_active(hotkey, keys)) {
        state.active = 0;
        state.delta = {0.0f, 0.0f, 0.0f};
        return;
    }
    if (state.active == 0) {
        state.base_angles = command.angles;
        state.active = 1;
    }
    const float base_pitch = state.base_angles[0];
    const float base_yaw = state.base_angles[1];
    const float yaw = state.delta[1] + (command.angles[1] - base_yaw);
    state.delta[0] = (command.angles[0] - base_pitch) + state.delta[0];
    const float maximum_pitch = 89.0f - base_pitch;
    const float minimum_pitch = -89.0f - base_pitch;
    if (state.delta[0] > maximum_pitch)
        state.delta[0] = maximum_pitch;
    else if (minimum_pitch > state.delta[0])
        state.delta[0] = minimum_pitch;
    const float normalized_yaw = normalize_movement_angle(yaw);
    const float lower_yaw = normalized_yaw > -170.0f ? normalized_yaw : -170.0f;
    state.delta[1] = lower_yaw < 170.0f ? lower_yaw : 170.0f;
    command.angles[0] = base_pitch;
    command.angles[1] = base_yaw;
    set_command_view_angles(bindings, command.angles);
    if (command.tick == 0)
        engine::invoke<void>(bindings.prediction, 13, command.angles.data());
}

} // namespace skeet::client
