#include "skeet/animation/local_helpers.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::animation {
namespace {
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU);
}
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (absolute(value) > 180.0f) {
        const float correction = round_half_away_from_zero(absolute(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value;
}
}
std::uint32_t movement_button_state(std::uint32_t buttons) noexcept {
    const bool forward = (buttons & 8U) != 0, backward = (buttons & 16U) != 0;
    const bool left = (buttons & 0x400U) != 0, right = (buttons & 0x200U) != 0;
    if (!(buttons & 0x1618U))
        return 0;
    if (forward && backward) {
        if ((left && right) || (!left && !right))
            return 0;
    } else if (left && right) {
        if (!forward && !backward)
            return 0;
    }
    return buttons & 0x20000U ? 1U : 2U;
}
void apply_movement_button_state(void* player, const client::Command& command,
                                 const LocalAnimationBindings& bindings) noexcept {
    auto* output = static_cast<std::byte*>(player) + bindings.offsets.movement_state;
    const std::uint32_t zero = 0;
    std::memcpy(output, &zero, sizeof(zero));
    const auto result = movement_button_state(command.movement.buttons);
    if (result)
        std::memcpy(output, &result, sizeof(result));
}
void update_lower_body_yaw_timer(float yaw, float speed, float vertical, float now, float target,
                                 float& next_update, float& previous_yaw) noexcept {
    const float normalized = normalize(yaw);
    float delay;
    if (!(speed <= 0.1f) || !(absolute(vertical) <= 100.0f)) {
        delay = std::bit_cast<float>(0x3E6147AEU);
    } else {
        if (next_update - now >= 0.0f)
            return;
        float delta = target - normalized;
        if (std::isnan(delta))
            return;
        if (absolute(delta) > 180.0f) {
            const float correction = round_half_away_from_zero(absolute(delta / 360.0f)) * 360.0f;
            delta = delta < 0.0f ? delta + correction : delta - correction;
        }
        if (delta < 0.0f)
            delta = std::bit_cast<float>(std::bit_cast<std::uint32_t>(delta) ^ 0x80000000U);
        if (delta <= 35.0f)
            return;
        delay = std::bit_cast<float>(0x3F8CCCCDU);
    }
    next_update = delay + now;
    if (previous_yaw != normalized)
        previous_yaw = normalized;
}
} // namespace skeet::animation
