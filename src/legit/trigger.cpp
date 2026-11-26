#include "skeet/legit/runtime.hpp"
#include "skeet/legit/tracking.hpp"
#include "skeet/engine/interface_call.hpp"
#include <immintrin.h>
#include <stdexcept>

namespace skeet::legit {
bool trigger_hit_allowed_live(const ProfileState& state, const TriggerHit& hit,
                              const TargetTrack* targets) {
    const auto damage = _mm_cvttss_si32(_mm_set_ss(hit.damage));
    if (damage < state.minimum_damage || !(state.trigger_hitgroups & (1u << (hit.hitgroup & 31u))))
        return false;
    if (state.trigger_reaction != 0.0f && hit.entity) {
        const auto index =
            engine::invoke<std::int32_t>(static_cast<std::byte*>(hit.entity) + 8, 10);
        if (state.trigger_reaction > targets[std::uint32_t(index) & 63u].elapsed)
            return false;
    }
    return true;
}
bool trigger_hit_allowed(const ProfileState& state, const TriggerHit& hit,
                         std::span<const float, 64> target_times) {
    const auto damage = _mm_cvttss_si32(_mm_set_ss(hit.damage));
    if (damage < state.minimum_damage || !(state.trigger_hitgroups & (1u << (hit.hitgroup & 31u))))
        return false;
    if (state.trigger_reaction != 0.0f && hit.entity) {
        const auto index =
            engine::invoke<std::int32_t>(static_cast<std::byte*>(hit.entity) + 8, 10);
        if (state.trigger_reaction > target_times[std::uint32_t(index) & 63u])
            return false;
    }
    return true;
}
bool begin_trigger(const Settings& settings, ProfileState& state, const TriggerFrame& frame,
                   InputBitmap input, TriggerCommand& command) {
    if (!(state.flags & 0x20u) || (command.buttons & 1u))
        return false;
    if (!hotkey_active(settings.trigger_keys[std::size_t(state.profile)].value.binding, input))
        return false;
    if (state.burst_until > frame.time)
        command.buttons |= 1u;
    if (!(frame.context_flags & 0x400u) && frame.weapon_id != 64)
        return false;
    if (!(state.flags & 8u) && frame.blind_amount > 200.0f)
        return false;
    return true;
}
void finish_trigger_live(ProfileState& state, const std::uint32_t* context_flags, const float* time,
                         bool hit, std::int32_t selected_tick, TriggerCommand& command,
                         RandomBindings random) {
    if (*context_flags & 0x400u) {
        if (!hit && !(command.buttons & 1u))
            return;
        if (selected_tick != -1)
            command.tick = selected_tick;
    }
    if (!hit)
        return;
    command.buttons |= 1u;
    if (state.flags & 0x40u)
        state.burst_until = state.burst_time + *time;
    else {
        if (!random.uniform)
            throw std::invalid_argument("Native trigger timer requires RandomFloat");
        const auto duration = random.uniform(random.context, 0.03125f, 0.1f);
        state.burst_until = duration + *time;
    }
}
void finish_trigger(ProfileState& state, const TriggerFrame& frame, bool hit,
                    std::int32_t selected_tick, TriggerCommand& command, RandomBindings random) {
    finish_trigger_live(state, &frame.context_flags, &frame.time, hit, selected_tick, command,
                        random);
}
} // namespace skeet::legit
