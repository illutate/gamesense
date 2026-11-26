#include "skeet/movement/duck_peek.hpp"
#include <bit>
#include <immintrin.h>

namespace skeet {
namespace {
std::int32_t decrement(std::int32_t value) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) - 1u);
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
}
std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
void force_send(std::uint32_t& flags) noexcept {
    flags = (flags & 0xfffffbffu) | 0x200u;
}
// Actual helper 4331E7F5, including the native cubic's operation order.
float duck_height(float amount) noexcept {
    const float square = amount * amount;
    const float doubled = square + square;
    const float smooth = square * 3.0f - doubled * amount;
    return (1.0f - smooth) * 18.0f + 46.0f;
}

void short_crouch_cycle(DuckPeekState& state, const DuckPeekFrame& frame, std::int32_t limit,
                        UserCommand& command, std::uint32_t& flags,
                        std::uint8_t& send_packet) noexcept {
    command.buttons |= duck_button;
    const float interval = frame.physics.interval_per_tick;
    const float amount = frame.physics.duck_amount;
    const float crouch_step = (interval * 0.8f) * 8.0f;
    if (state.phase == 0) {
        const float threshold = 1.0f - interval * 8.0f;
        if (!(absolute((crouch_step + amount) - threshold) <= 0.001f)) {
            if (absolute(amount - threshold) <= 0.001f) {
                const auto total = limit + 1;
                const auto release = total / 2;
                const auto crouch = total - release;
                state.release_commands = release;
                state.crouch_commands = crouch;
                const float release_height = (interval * 8.0f) * static_cast<float>(release);
                const float crouch_height = ((interval * 0.8f) * 8.0f) * static_cast<float>(crouch);
                state.delay = 0;
                float best_difference = 0.0f;
                for (std::int32_t delay = 0; delay < 10; ++delay) {
                    const float target =
                        (1.0f - (interval * 8.0f) * static_cast<float>(delay + 1)) - release_height;
                    const float first = duck_height(target);
                    const float second = duck_height(target + crouch_height);
                    const float difference = first - second;
                    if (best_difference >= difference)
                        break;
                    state.delay = delay;
                    state.target = target;
                    best_difference = difference;
                }
                force_send(flags);
                state.phase = 1;
            } else if (amount > threshold) {
                send_packet = 0;
                command.buttons &= ~duck_button;
            }
        }
    }

    // A completed phase enters its successor during this same command.
    if (state.phase == 1) {
        if (state.delay > 0) {
            state.delay = decrement(state.delay);
            command.buttons &= ~duck_button;
            force_send(flags);
        } else
            state.phase = 2;
    }
    if (state.phase == 2) {
        if (state.release_commands > 0) {
            state.release_commands = decrement(state.release_commands);
            if (state.choked < limit) {
                command.buttons &= ~duck_button;
                send_packet = 0;
            }
        } else
            state.phase = 3;
    }
    if (state.phase == 3) {
        if (state.crouch_commands == 0)
            state.phase = 0;
        else {
            state.crouch_commands = decrement(state.crouch_commands);
            if (state.choked < limit)
                send_packet = 0;
        }
    }
    if ((command.buttons & duck_button) && crouch_step + amount > 0.99f)
        command.buttons &= ~duck_button;
}

void long_crouch_cycle(DuckPeekState& state, const DuckPeekFrame& frame, std::int32_t limit,
                       UserCommand& command, std::uint8_t& send_packet) noexcept {
    state.target = 0.0f;
    command.buttons |= duck_button;
    const float interval = frame.physics.interval_per_tick;
    const float release_step = interval * 8.0f;
    const float crouch_step = (interval * 0.8f) * 8.0f;
    const float product = release_step * crouch_step;
    const float sum = release_step + crouch_step;
    float target = static_cast<float>(limit) / (sum / product);
    // MAXSS with zero, then MINSS with one: unordered selects operand two.
    if (!(target > 0.0f))
        target = 0.0f;
    if (!(target < 1.0f))
        target = 1.0f;
    const float amount = frame.physics.duck_amount;
    bool advance = state.crouch_active;
    if (amount >= target) {
        state.crouch_active = true;
        advance = true;
    }
    if (advance && state.choked < limit) {
        const auto crouch_ticks = truncate(target / crouch_step + 0.5f);
        const auto remaining =
            std::bit_cast<std::int32_t>(std::uint32_t(limit) - std::uint32_t(state.choked));
        if (remaining >= crouch_ticks || amount > target)
            command.buttons &= ~duck_button;
        send_packet = 0;
    }
    if ((command.buttons & duck_button) && amount + crouch_step > 0.99f)
        command.buttons &= ~duck_button;
}
} // namespace

void process_duck_peek(const DuckPeekSettings& settings, InputBitmap input, DuckPeekState& state,
                       const DuckPeekFrame& frame, UserCommand& command, std::uint32_t& flags,
                       std::uint8_t& send_packet) {
    const bool original_duck = (state.original_buttons & duck_button) != 0;
    const bool separate_duck = original_duck && state.last_key != 27 &&
                               std::uint32_t(state.last_key) != (settings.hotkey.packed >> 2);
    if (!(frame.physics.player_flags & on_ground) || (command.buttons & jump_button) ||
        (!hotkey_active(settings.hotkey, input) && !(flags & 0x100u))) {
        state.crouch_active = false;
        state.release_latch = false;
        state.target = 0.0f;
        if (separate_duck)
            flags |= 2u;
        return;
    }
    if (!settings.crouch_cycle) {
        const auto velocity = frame.velocity;
        const float speed_squared =
            (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
        if (!(speed_squared <= 10.0f))
            return;
        command.buttons |= duck_button;
        if (frame.has_weapon) {
            const float elapsed = frame.current_time - frame.weapon_simulation_time;
            const float delay = maximum_number(frame.duck_delay, frame.weapon_duck_delay) + 0.75f;
            if (elapsed > delay)
                command.buttons &= ~duck_button;
        }
        return;
    }
    if (separate_duck) {
        flags |= 0x42u;
        if (!(state.previous_buttons & duck_button))
            state.release_latch = true;
        else if (!state.release_latch)
            return;
        if (state.choked < 14 && frame.physics.duck_amount < 1.0f) {
            send_packet = 0;
            return;
        }
        state.release_latch = false;
        return;
    }
    if (frame.player_duck_time + frame.duck_delay > frame.current_time) {
        flags |= 0x42u;
        command.buttons |= duck_button;
        state.release_latch = false;
        return;
    }
    state.release_latch = false;
    // The native JA only excludes ordered rates below six.
    if (6.0f > frame.physics.duck_speed)
        return;
    const auto budget_limit = decrement(frame.command_budget);
    const auto configured = *state.configured_limit;
    const auto limit = budget_limit < configured ? budget_limit : configured;
    if (limit < 3) {
        state.target = 0.0f;
        return;
    }
    flags |= 0x40u;
    if (limit <= 9)
        short_crouch_cycle(state, frame, limit, command, flags, send_packet);
    else
        long_crouch_cycle(state, frame, limit, command, send_packet);
}

} // namespace skeet
