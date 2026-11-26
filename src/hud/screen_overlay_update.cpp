#include "skeet/hud/screen_overlay_update.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/events/game_events.hpp"
#include "skeet/core.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::hud {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void assign(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
const void* parent(const void* variable) {
    for (;;) {
        const auto next = field<const void*>(variable, 0x1C);
        if (variable == next)
            return variable;
        variable = next;
    }
}
float variable_float(const void* variable) {
    variable = parent(variable);
    return std::bit_cast<float>(
        field<std::uint32_t>(variable, 0x2C) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable)));
}
std::int32_t variable_integer(const void* variable) {
    variable = parent(variable);
    return std::bit_cast<std::int32_t>(
        field<std::uint32_t>(variable, 0x30) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable)));
}
void* find_variable(void* cvar) { // complete4332A793 for hash3EEAB5A6
    for (auto node = field<void*>(cvar, 0x30); node; node = field<void*>(node, sizeof(void*)))
        if (events::event_hash(engine::invoke<const char*>(node, 6)) == 0x3EEAB5A6)
            return node;
    return nullptr;
}
bool supported_weapon(std::uint16_t definition) { // complete433D24BB
    constexpr std::uint32_t words[] = {0xFF8B6F9E, 0xB00001DF, 1};
    return definition < 65 &&
           (words[definition >> 5] & (std::uint32_t{1} << (definition & 31))) != 0;
}
float negate(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000);
}
float absolute(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFF);
}
std::int32_t decrement(std::int32_t value) {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) - 1);
}
}
void bind_screen_overlay_recoil_config(ScreenOverlayState& state,
                                       const rage::GlobalSettings& settings) noexcept {
    state.byte_58 = &settings.remove_recoil.value;
}
void reset_screen_overlay_state(ScreenOverlayState& state) noexcept {
    state.radius_5c = 0;
    if (state.byte_6c)
        state.byte_6c = false;
}
ScreenOverlayCommandFrame screen_overlay_command_frame(const void* context) {
    ScreenOverlayCommandFrame result{};
    result.flags = field<std::uint32_t>(context, 0);
    result.weapon = field<void*>(context, 0xC);
    result.recoil_view = {field<float>(context, 0x9C), field<float>(context, 0xA0)};
    result.spread.inaccuracy = field<float>(context, 0xC4);
    result.spread.spread = field<float>(context, 0xD0);
    return result;
}
void ScreenOverlayPublisher::publish(ScreenOverlayState& state,
                                     const esp::ScreenOverlayConfig& config,
                                     const ScreenOverlayCommandFrame& frame, void* local,
                                     const client::Command& command,
                                     const ScreenOverlayFields& fields,
                                     const ScreenOverlayUpdateBindings& bindings) {
    if (state.byte_6c)
        state.byte_6c = false;
    state.radius_5c = 0;
    if (config.recoil.value && frame.weapon && !*state.byte_58 &&
        supported_weapon(field<std::uint16_t>(frame.weapon, fields.weapon_definition)) &&
        !(1.0f > field<float>(frame.weapon, fields.weapon_recoil_index))) {
        const auto length = frame.recoil_view[0] * frame.recoil_view[0] +
                            frame.recoil_view[1] * frame.recoil_view[1];
        if (!(0.001f >= length)) {
            auto angle = field<RageVector>(local, fields.recoil_angles);
            auto velocity = field<RageVector>(local, fields.recoil_velocity);
            const auto weapon_time = field<float>(frame.weapon, fields.weapon_time_f71ce331);
            auto current_time = *bindings.current_time;
            const auto time_delta = weapon_time - current_time;
            if (time_delta > negate(*bindings.interval) && time_delta < 0.75f) {
                const auto local_time = field<float>(local, fields.player_time_0176a381);
                const auto age = current_time - local_time;
                if (age >= 0 && age < 2.0f) {
                    while (current_time < weapon_time) {
                        const auto decay =
                            negate(variable_float(bindings.first_decay) * 2.3025851249694824f) *
                            *bindings.receive_interval;
                        // The caller makes this CRT call even though4336B421
                        // replaces XMM0 before consuming it.
                        (void)bindings.exponential(decay);
                        legit::decay_recoil(velocity, angle,
                                            {bindings.linear_decay, bindings.angle_decay,
                                             bindings.velocity_decay, *bindings.interval,
                                             bindings.exponential});
                        current_time += *bindings.interval;
                    }
                    auto previous = field<RageVector>(local, fields.recoil_angles);
                    const auto* identity = engine::invoke<const std::uint32_t*>(frame.weapon, 2);
                    auto sequence = decrement(command.number);
                    for (unsigned scanned = 0; scanned < 150;
                         ++scanned, sequence = decrement(sequence)) {
                        const auto* record =
                            prediction::CommandHistoryView::from_owner(bindings.history_owner)
                                .find(sequence);
                        if (!record)
                            continue;
                        if (record->weapon_identity != *identity || record->timestamp != local_time)
                            break;
                        previous = record->recoil_angles;
                    }
                    const auto difference = absolute(current_time - local_time);
                    if (difference != 0) {
                        auto fraction = (*bindings.current_time - local_time) / difference;
                        fraction = fraction > 0 ? fraction : 0;
                        fraction = fraction < 1 ? fraction : 1;
                        if (angle[0] == previous[0] && angle[1] == previous[1] &&
                            angle[2] == previous[2])
                            angle = previous;
                        else {
                            const auto first =
                                lagcomp::history_angle_quaternion(previous, bindings.rotation);
                            const auto second =
                                lagcomp::history_angle_quaternion(angle, bindings.rotation);
                            angle = lagcomp::history_quaternion_angles(
                                lagcomp::slerp_history_quaternion(
                                    first, lagcomp::align_history_quaternion(first, second),
                                    fraction, bindings.rotation),
                                bindings.rotation);
                        }
                    }
                }
            }
            const auto scale = variable_float(bindings.recoil_scale);
            state.recoil_pitch_60 = angle[0] * scale;
            state.recoil_yaw_64 = angle[1] * scale;
            state.recoil_roll_68 = angle[2] * scale;
            state.byte_6c = true;
        }
    }
    if (!frame.weapon || !config.inaccuracy.value || (frame.flags & 0x4000) != 0 ||
        !supported_weapon(field<std::uint16_t>(frame.weapon, fields.weapon_definition)))
        return;
    auto spread = frame.spread.inaccuracy + frame.spread.spread;
    if (!prediction_variable_initialized) {
        prediction_variable_initialized = true;
        prediction_variable = find_variable(bindings.cvar);
    }
    if (prediction_variable && variable_integer(prediction_variable) == 0) {
        const auto weapon_time = field<float>(frame.weapon, fields.weapon_time_f71ce331);
        const auto next =
            maximum_number(field<float>(local, fields.player_time_de530a8b), weapon_time);
        const auto quotient = (next - *bindings.current_time) / *bindings.interval;
        const auto rounded = _mm_cvtss_si32(_mm_set_ss(quotient));
        const auto rounded_float = _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), rounded));
        auto count = std::uint32_t(rounded);
        if (quotient > rounded_float)
            ++count;
        if (count - 1 <= 0x7E) {
            const auto penalty = field<float>(frame.weapon, fields.weapon_penalty_d2563274);
            const auto recoil = field<float>(frame.weapon, fields.weapon_recoil_index);
            for (std::uint32_t tick = 0; tick < count; ++tick)
                engine::invoke<void>(frame.weapon, 484);
            spread = engine::invoke<float>(frame.weapon, 483) + frame.spread.spread;
            assign(frame.weapon, fields.weapon_recoil_index, recoil);
            assign(frame.weapon, fields.weapon_penalty_d2563274, penalty);
        }
    }
    std::int32_t width, height;
    engine::invoke<void>(bindings.engine, 5, &width, &height);
    const auto radians = (*bindings.field_of_view * 0.01745329238474369f) * 0.5f;
    const auto scaled = spread * 320.0f;
    const auto ratio = scaled / bindings.tangent(radians);
    const auto screen = _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(height))) / 480.0f;
    auto radius = screen * ratio;
    if (engine::invoke<std::int32_t>(bindings.input, 32, -1))
        radius *= 0.9f;
    state.radius_5c = radius;
}
} // namespace skeet::hud
