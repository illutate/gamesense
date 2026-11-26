#include "skeet/legit/recoil_state.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
float scale_value(const void* convar) {
    for (;;) {
        const auto* parent = read<const void*>(convar, 0x1C);
        if (parent == convar)
            break;
        convar = parent;
    }
    return std::bit_cast<float>(read<std::uint32_t>(convar, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(convar)));
}
void normalize(Vector& vector) {
    for (auto& component : vector)
        component = normalize_movement_angle(component);
}
}
void update_recoil_state(RuntimeState& state, const RecoilStateInput& input,
                         const void* recoil_scale, const RecoilDecayBindings& decay,
                         RandomBindings random) {
    auto& profile = state.profile;
    if (!(profile.secondary_flags & 1u) && !profile.enabled)
        return;
    if (input.player_punch[0] == 0.0f)
        state.recoil_velocity[0] = 0.0f;
    if (input.player_punch[1] == 0.0f)
        state.recoil_velocity[1] = 0.0f;
    auto velocity = input.context_velocity;
    decay_recoil(velocity, state.recoil_velocity, decay);
    if ((input.command_buttons & 1u) && state.new_shot) {
        const auto next_attack = maximum_number(input.weapon_next_attack, input.player_next_attack);
        auto future_velocity = input.player_velocity;
        auto future_angle = state.recoil_velocity;
        auto time = input.time;
        std::int32_t iterations = 1;
        if (time < next_attack)
            do {
                decay_recoil(future_velocity, future_angle, decay);
                ++iterations;
                time += decay.interval;
            } while (time < next_attack);
        const auto scale = scale_value(recoil_scale);
        auto& delta = profile.recoil_history[2];
        const auto& accumulated = profile.recoil_history[1];
        delta[0] = (profile.recoil_pitch * scale) * future_angle[0] - accumulated[0];
        delta[1] = (profile.recoil_yaw * scale) * future_angle[1] - accumulated[1];
        delta[2] = future_angle[2] - accumulated[2];
        normalize(delta);
        if (iterations > 1) {
            const auto inverse = 1.0f / float(iterations);
            for (auto& component : delta)
                component *= inverse;
        }
        if (!random.uniform)
            throw std::invalid_argument("LEGIT recoil state requires native RandomFloat");
        const auto maximum = random.uniform(random.context, 0.2f, 0.3f) * state.time_scale;
        const auto length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1]);
        if (maximum < length) {
            const auto fraction = maximum / length;
            for (auto& component : delta)
                component *= fraction;
        }
    }
    const auto scale = scale_value(recoil_scale);
    profile.recoil_history[0][0] = (profile.recoil_pitch * scale) * state.recoil_velocity[0];
    profile.recoil_history[0][1] = (profile.recoil_yaw * scale) * state.recoil_velocity[1];
}
Vector recoil_reference_angles(const RuntimeState& state, const Vector& frame,
                               const void* recoil_scale) {
    const auto scale = scale_value(recoil_scale);
    return {state.recoil_velocity[0] * scale + frame[0],
            state.recoil_velocity[1] * scale + frame[1], 0.0f};
}
void add_player_recoil(Vector& angles, const Vector& punch, const void* recoil_scale,
                       bool normalized) {
    if (punch[0] == 0.0f && punch[1] == 0.0f)
        return;
    const auto scale = scale_value(recoil_scale);
    for (unsigned axis = 0; axis != 3; ++axis)
        angles[axis] = punch[axis] * scale + angles[axis];
    if (normalized)
        normalize(angles);
}
bool recoil_shot_ready(const ProfileState& profile, void* weapon, const client::Command& command,
                       std::ptrdiff_t ammunition) {
    if (!engine::invoke<bool>(weapon, 430))
        return false;
    if (!(command.movement.buttons & 1u) || profile.shots <= 1)
        return false;
    return read<std::int32_t>(weapon, ammunition) > 0;
}
} // namespace skeet::legit
