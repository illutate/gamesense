#include "skeet/legit/state.hpp"
#include <immintrin.h>
#include <stdexcept>

namespace skeet::legit {
void release_target(RuntimeState& state, float interval, RandomBindings random) {
    if (!state.target)
        return;
    state.target = 0;
    if (!random.seed || !random.uniform)
        throw std::invalid_argument("Target release requires native RandomSeed/RandomFloat");
    const auto ticks = _mm_cvttss_si32(_mm_set_ss(1.0f / interval));
    if (!ticks)
        throw std::domain_error("Native target-release interval has no tick divisor");
    random.seed(random.context, state.last_command / ticks);
    state.curve = random.uniform(random.context, -1.5f, 1.5f);
}
void reset_runtime_live(RuntimeState& state, const float* interval, RandomBindings random) {
    state.pending_step = {};
    state.remainder = {};
    state.profile.previous_buttons = 0;
    state.new_shot = 0;
    state.profile.shots = 0;
    clear_recoil_history(state.profile);
    release_target(state, *interval, random);
    state.recoil_velocity = {};
    state.profile.burst_until = 0;
    state.block_until = 0;
    state.filtered_motion = {};
    state.manual_time = 0;
    state.profile.secondary_flags &= 0xF9u;
    state.pending_fraction = 0;
    state.target = state.previous_target = 0;
    state.current_view_valid = state.previous_view_valid = 0;
    for (auto& target : state.targets) {
        target.elapsed = 0;
        target.visible_hitgroups = 0;
        target.point = {};
        target.velocity_valid = 0;
        target.interval = *interval;
    }
}
void reset_runtime(RuntimeState& state, float interval, RandomBindings random) {
    reset_runtime_live(state, &interval, random);
}
void apply_pending_aim(RuntimeState& state, client::Command& command, float delta_time,
                       float interval) {
    if (state.pending_fraction <= 0.0f) {
        state.pending_fraction = 0;
        return;
    }
    const auto fraction = _mm_min_ss(
        _mm_max_ss(_mm_set_ss(delta_time / interval), _mm_setzero_ps()), _mm_set_ss(1.0f));
    const auto applied = _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(state.pending_fraction), fraction));
    const auto pitch = state.pending_step[0] * applied;
    const auto yaw = state.pending_step[1] * applied;
    command.angles[0] += pitch;
    command.angles[1] += yaw;
    state.pending_fraction -= applied;
    if (!command.tick) {
        state.remainder[0] = pitch + state.remainder[0];
        state.remainder[1] = yaw + state.remainder[1];
    }
}
} // namespace skeet::legit
