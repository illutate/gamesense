#include "skeet/client/command_transform.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace skeet::client {
namespace {
using Vector = std::array<float, 3>;
float neg(float f) {
    return std::bit_cast<float>(std::bit_cast<unsigned>(f) ^ 0x80000000u);
}
float absolute(float f) {
    return std::bit_cast<float>(std::bit_cast<unsigned>(f) & 0x7FFFFFFFu);
}
void basis(Vector angles, Vector& forward, Vector& right, Vector& up,
           const CommandTrigonometry& trig) {
    constexpr float radians = 0.01745329238474369f;
    const float sp = trig.sine(angles[0] * radians), cp = trig.cosine(angles[0] * radians);
    const float sy = trig.sine(angles[1] * radians), cy = trig.cosine(angles[1] * radians);
    const float sr = trig.sine(angles[2] * radians), cr = trig.cosine(angles[2] * radians);
    const float minus_sr = sr * -1.0f, minus_cr = cr * -1.0f;
    forward = {cy * cp, sy * cp, neg(sp)};
    right = {minus_cr * neg(sy) - sr * sp * cy, minus_cr * cy - sr * sp * sy, minus_sr * cp};
    up = {cr * sp * cy - neg(sy) * sr, cr * sp * sy - sr * cy, cr * cp};
}
void horizontal(Vector& vector) {
    if (vector[2] == 0.0f)
        return;
    vector[2] = 0.0f;
    const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1]);
    if (length >= std::numeric_limits<float>::epsilon()) {
        const float inverse = 1.0f / length;
        vector[0] *= inverse;
        vector[1] = inverse * vector[1];
    } else
        vector[0] = vector[1] = 0.0f;
}
float clamp(float f, float low, float high) {
    f = f > low ? f : low;
    return f < high ? f : high;
}
}
void rebuild_command_direction_buttons(Command& command, bool force,
                                       const CommandButtonPolicy& policy) noexcept {
    auto buttons = command.movement.buttons & ~0x618u;
    auto mode = policy.mode;
    const auto number = std::uint32_t(command.number);
    if (policy.inverted_commands[number % 150u] == number)
        mode = 1;
    if (mode == 1 && policy.choked != 0 && !force) {
        command.movement.buttons = buttons;
        return;
    }
    const float forward = command.movement.forward_move, side = command.movement.side_move;
    if (mode == 0) {
        if (forward > 0.0f)
            buttons |= 8u;
        else if (forward < 0.0f)
            buttons |= 0x10u;
        if (side > 0.0f)
            buttons |= 0x400u;
        else if (side < 0.0f)
            buttons |= 0x200u;
    } else if (mode == 1) {
        if (forward > 0.0f)
            buttons |= 0x10u;
        else if (forward < 0.0f)
            buttons |= 8u;
        if (side > 0.0f)
            buttons |= 0x200u;
        else if (side < 0.0f)
            buttons |= 0x400u;
    }
    command.movement.buttons = buttons;
}
void reorient_command_movement(Command& command, const Vector& original, std::uint8_t type,
                               bool grounded, bool force, const CommandButtonPolicy& policy,
                               const CommandTrigonometry& trig) {
    auto& move = command.movement;
    if (type == 8 || type == 9)
        return;
    if (move.forward_move * move.forward_move + move.side_move * move.side_move == 0.0f)
        return;
    if (original != command.angles) {
        Vector old_forward, old_right, old_up, new_forward, new_right, new_up;
        basis({original[0], original[1], 0.0f}, old_forward, old_right, old_up, trig);
        horizontal(old_forward);
        horizontal(old_right);
        const float old_vertical = old_up[2] >= std::numeric_limits<float>::epsilon() ? 1.0f : 0.0f;
        if (!grounded && command.angles[2] == 180.0f)
            move.forward_move = neg(move.forward_move);
        basis(command.angles, new_forward, new_right, new_up, trig);
        horizontal(new_forward);
        horizontal(new_right);
        const float new_vertical = new_up[2] >= std::numeric_limits<float>::epsilon() ? 1.0f : 0.0f;
        for (unsigned i = 0; i != 3; ++i) {
            old_forward[i] *= move.forward_move;
            old_right[i] *= move.side_move;
        }
        const float vertical = old_vertical * move.up_move, zero = move.up_move * 0.0f;
        if (absolute(command.angles[2]) == 0.0f) {
            const auto project = [&](const Vector& target) {
                return ((((((((old_right[1] * target[1] + old_right[0] * target[0]) +
                              old_right[2] * target[2]) +
                             old_forward[1] * target[1]) +
                            old_forward[0] * target[0]) +
                           old_forward[2] * target[2]) +
                          zero * target[1]) +
                         zero * target[0]) +
                        vertical * target[2]);
            };
            move.forward_move = project(new_forward);
            move.side_move = project(new_right);
            move.up_move =
                (((((((old_right[1] * 0.0f + old_right[0] * 0.0f) + old_right[2] * new_vertical) +
                     old_forward[1] * 0.0f) +
                    old_forward[0] * 0.0f) +
                   old_forward[2] * new_vertical) +
                  vertical * new_vertical) +
                 zero * 0.0f) +
                zero * 0.0f;
        } else {
            const float first = new_right[0] * new_forward[1],
                        second = new_right[1] * new_forward[0];
            if (first != second && first != 0.0f) {
                move.side_move = ((old_right[1] + old_forward[1]) * new_forward[0] -
                                  (old_right[0] + old_forward[0]) * new_forward[1]) /
                                 (second - first);
                move.forward_move = ((old_right[1] + old_forward[1]) * new_right[0] -
                                     (old_right[0] + old_forward[0]) * new_right[1]) /
                                    (first - second);
                const float side = absolute(move.side_move), forward = absolute(move.forward_move);
                float scale = 1.0f;
                if (side > forward) {
                    if (side > 450.0f)
                        scale = 450.0f / side;
                } else if (forward > side && forward > 450.0f)
                    scale = 450.0f / forward;
                if (scale != 1.0f) {
                    move.side_move *= scale;
                    move.forward_move *= scale;
                    move.up_move *= scale;
                }
            }
        }
    }
    rebuild_command_direction_buttons(command, force, policy);
}
void sanitize_command(Command& command, bool unrestricted,
                      const ScriptCommandEnvironment& environment) noexcept {
    if (!unrestricted) {
        command.angles[0] = clamp(normalize_movement_angle(command.angles[0]), -89.0f, 89.0f);
        command.angles[1] = clamp(normalize_movement_angle(command.angles[1]), -180.0f, 180.0f);
        if (!command_roll_blocked(environment))
            command.angles[2] = clamp(normalize_movement_angle(command.angles[2]), -45.0f, 45.0f);
        else
            command.angles[2] = 0.0f;
    } else
        command.angles[1] = clamp(normalize_movement_angle(command.angles[1]), -180.0f, 180.0f);
    for (float& angle : command.angles)
        if (std::isnan(angle))
            angle = 0.0f;
    auto& move = command.movement;
    if (std::isnan(move.side_move))
        move.side_move = 0.0f;
    if (std::isnan(move.forward_move))
        move.forward_move = 0.0f;
    if (std::isnan(move.up_move))
        move.up_move = 0.0f;
    move.forward_move = clamp(move.forward_move, -450.0f, 450.0f);
    move.side_move = clamp(move.side_move, -450.0f, 450.0f);
    move.up_move = clamp(move.up_move, -320.0f, 320.0f);
}
} // namespace skeet::client
