#include "skeet/legit/runtime.hpp"
#include <stdexcept>

namespace skeet::legit {
bool recoil_compensation_enabled(const Settings* settings, std::uint32_t category,
                                 InputBitmap input) {
    return !settings ||
           (settings->enabled.value[category] &&
            hotkey_active(settings->aim_keys[category].value.binding, input)) ||
           settings->standalone_recoil.value[category];
}
void clear_recoil_history(ProfileState& state) noexcept {
    state.recoil_history = {};
}
bool select_weapon_profile(const Settings& s, ProfileState& state, std::int32_t requested,
                           std::uint32_t command_buttons) {
    if (requested < 0)
        throw std::invalid_argument("Native weapon profile must be a nonnegative signed byte");
    const auto i = std::size_t(requested < 9 ? requested : 0);
    if (requested < 9)
        state.profile = requested;
    state.accuracy_boost = s.accuracy_boost.value[i];
    state.accuracy_range = s.accuracy_range.value[i];
    state.secondary_flags = (state.secondary_flags & 0xFEu) | (s.standalone_recoil.value[i] & 1u);
    state.enabled = s.enabled.value[i];
    state.flags = (state.flags & 0xDFu) | ((s.trigger_enabled.value[i] << 5) & 0x20u);
    if (!state.accuracy_boost && !(state.secondary_flags & 1u) && !state.enabled &&
        !(state.flags & 0x20u)) {
        clear_recoil_history(state);
        return false;
    }
    state.speed =
        float((command_buttons & 1u) ? s.attack_speed.value[i] : s.speed.value[i]) / 500.0f;
    state.speed_scale_fov = float(s.speed_scale_fov.value[i]) * 0.01f;
    if (s.maximum_lock_time.value[i] == 1001)
        state.limit_lock_time = false;
    else {
        state.limit_lock_time = true;
        state.maximum_lock_time = float(s.maximum_lock_time.value[i]) * 0.001f;
    }
    state.reaction_time = float(s.reaction_time.value[i]) * 0.001f;
    state.maximum_fov = minimum_number(60.0f, float(s.maximum_fov.value[i]) * 0.1f);
    state.recoil_pitch = float(s.recoil_pitch.value[i]) * 0.01f;
    state.recoil_yaw = float(s.recoil_yaw.value[i]) * 0.01f;
    auto flag = [&](std::uint8_t value, unsigned bit) {
        const auto mask = 1u << bit;
        state.flags = std::uint8_t((state.flags & ~mask) | ((unsigned(value) << bit) & mask));
    };
    flag(s.quick_stop.value[i], 0);
    flag(s.through_smoke.value[i], 1);
    flag(s.while_blind.value[i], 2);
    flag(s.shoot_smoke.value[i], 4);
    flag(s.shoot_blind.value[i], 3);
    state.aim_hitgroups =
        (s.head.value[i] ? 2u : 0u) | (s.chest.value[i] ? 4u : 0u) | (s.stomach.value[i] ? 8u : 0u);
    state.first_shot_range = (s.aim_keys[i].value.binding.packed & 0xFFFFFFFCu) == 4
                                 ? float(s.first_shot_delay.value[i])
                                 : 0.0f;
    state.minimum_hit_chance = s.minimum_hit_chance.value[i];
    state.trigger_reaction = float(s.trigger_reaction.value[i]) * 0.001f;
    flag(s.burst_fire.value[i], 6);
    state.burst_time = float(s.burst_time.value[i]) * 0.001f;
    state.minimum_damage = s.minimum_damage.value[i];
    flag(s.penetration.value[i], 7);
    state.trigger_hitgroups = (s.trigger_head.value[i] ? 2u : 0u) |
                              (s.trigger_chest.value[i] ? 4u : 0u) |
                              (s.trigger_stomach.value[i] ? 8u : 0u);
    return true;
}
} // namespace skeet::legit
