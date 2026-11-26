#include "skeet/misc/knife_aim.hpp"
#include "skeet/misc/knife_melee.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/rage/firing.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float bound_component(float value, float minimum, float maximum) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(minimum)), _mm_set_ss(maximum)));
}
float add(float first, float second) noexcept {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(first), _mm_set_ss(second)));
}
float subtract(float first, float second) noexcept {
    return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(first), _mm_set_ss(second)));
}
float multiply(float first, float second) noexcept {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(first), _mm_set_ss(second)));
}
float square_root(float value) noexcept {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value)));
}
float inverse_length(float length) noexcept {
    return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(1.0f), _mm_set_ss(add(length, 0x1p-23f))));
}
float squared_length(const RageVector& vector) noexcept {
    return add(add(multiply(vector[0], vector[0]), multiply(vector[1], vector[1])),
               multiply(vector[2], vector[2]));
}
MovementAngles command_angles(const client::Command& command) noexcept {
    return {command.angles[0], command.angles[1], command.angles[2]};
}
} // namespace

void evaluate_knife_direction(std::uint32_t modes, const client::CommandFrame& frame,
                              void* local_player, void* target, bool low_health,
                              RageVector& chosen_direction, std::uint32_t& best_score,
                              const KnifeMeleeBindings& bindings) {
    auto* collideable = engine::invoke<void*>(target, 3);
    const auto* maximum = engine::invoke<const RageVector*>(collideable, 2);
    collideable = engine::invoke<void*>(target, 3);
    const auto* minimum = engine::invoke<const RageVector*>(collideable, 1);
    const auto origin = field<RageVector>(target, bindings.target_origin);
    RageVector direction;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float nearest = bound_component(subtract(frame.origin_dc[axis], origin[axis]),
                                              (*minimum)[axis], (*maximum)[axis]);
        direction[axis] = subtract(add(origin[axis], nearest), frame.origin_dc[axis]);
    }
    const float distance = square_root(squared_length(direction));
    const float inverse = inverse_length(distance);
    for (auto& component : direction)
        component = multiply(inverse, component);
    if (!(distance <= 100.0f))
        return;
    if (!find_knife_trace_direction(local_player, frame.origin_dc, target, direction, bindings))
        return;

    const bool swing_enabled = (modes & 1U) != 0;
    const bool stab_enabled = (modes & 2U) != 0;
    auto swing_score =
        swing_enabled ? score_knife_attack(frame, local_player, direction, false, bindings) : 0U;
    const auto stab_score =
        stab_enabled ? score_knife_attack(frame, local_player, direction, true, bindings) : 0U;

    if ((frame.flags & 0x400U) && swing_enabled) {
        if (stab_enabled && stab_score == 0 && (swing_score & 4U) && !(swing_score & 0x10U)) {
            const float speed = square_root(squared_length(frame.velocity_150));
            const float velocity_inverse = inverse_length(speed);
            const float alignment =
                add(add(multiply(multiply(frame.velocity_150[1], velocity_inverse), direction[1]),
                        multiply(multiply(frame.velocity_150[0], velocity_inverse), direction[0])),
                    multiply(multiply(frame.velocity_150[2], velocity_inverse), direction[2]));
            if (alignment >= 0.08715574443340302f)
                swing_score = 0;
        }
        if (best_score < swing_score && (!stab_enabled || !low_health || (swing_score & 0x10U))) {
            best_score = swing_score;
            chosen_direction = direction;
            if (swing_score & 0x10U)
                return;
        }
    }
    if ((frame.flags & 2U) && stab_enabled && best_score < stab_score) {
        best_score = stab_score;
        chosen_direction = direction;
    }
}

bool apply_knife_aim(const client::CommandFrame& frame, void* local_player,
                     client::Command& command, const RageVector& direction,
                     std::uint8_t& send_packet, const KnifeAimBindings& bindings) {
    constexpr float degrees = 57.2957763671875f;
    if (direction[0] == 0.0f && direction[1] == 0.0f) {
        command.angles = {direction[2] > 0.0f ? 270.0f : 90.0f, 0.0f, 0.0f};
    } else {
        const float yaw = multiply(
            float(bindings.arc_tangent2(double(direction[1]), double(direction[0]))), degrees);
        const float horizontal = square_root(
            add(multiply(direction[0], direction[0]), multiply(direction[1], direction[1])));
        const float pitch = multiply(
            float(bindings.arc_tangent2(double(-direction[2]), double(horizontal))), degrees);
        command.angles = {pitch, yaw, 0.0f};
    }
    if (frame.flags & 0x40U) {
        auto reference = command_angles(frame.previous_command);
        if (!(field<std::uint32_t>(local_player, bindings.player_flags) & 8U) &&
            field<std::uint8_t>(local_player, bindings.first_player_byte) ==
                field<std::uint8_t>(local_player, bindings.second_player_byte)) {
            const auto number = std::bit_cast<std::int32_t>(std::uint32_t(command.number) - 1U);
            if (const auto* previous =
                    engine::invoke<const client::Command*>(bindings.input, 8, 0, number))
                reference = command_angles(*previous);
        }
        auto aim = command_angles(command);
        const bool limited =
            rage::limit_firing_angle_step(aim, reference, frame.maximum_angle_step_1e8);
        command.angles = {aim.pitch, aim.yaw, aim.roll};
        if (limited)
            return false;
    }
    send_packet = 0;
    return true;
}
} // namespace skeet::misc
