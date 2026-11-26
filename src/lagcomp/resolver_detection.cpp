#include "skeet/lagcomp/resolver_detection.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>

namespace skeet::lagcomp {
namespace {
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (std::fabs(value) > 180.0f) {
        const float turns = round_half_away_from_zero(std::fabs(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + turns : value - turns;
    }
    return value;
}
}
bool newest_packet_timing_discontinuity(const LagTimeline& timeline) noexcept {
    if (!timeline.packet_count)
        return false;
    const auto& packet = timeline.packets[timeline.packet_head % 6u];
    const auto last = std::bit_cast<std::int32_t>(std::uint32_t(packet.field_18) - 1u);
    return packet.field_00 < last;
}
bool update_resolver_detection(ResolverDetectionState& state,
                               const ResolverDetectionInput& input) noexcept {
    if (state.enabled)
        return true;
    if (newest_packet_timing_discontinuity(input.packets)) {
        state.enabled = true;
        return true;
    }
    if (input.simulation_ticks > 1) {
        const auto delta = normalize(input.previous_yaw_118 - input.observed_yaw_1ac);
        const float elapsed = float(input.simulation_ticks) * input.interval;
        if (std::fabs(delta) > 40.0f)
            state.yaw_duration_1c0 += elapsed;
        else
            state.yaw_duration_1c0 = 0.0f;
        if (input.pitch > 70.0f)
            state.pitch_duration_178 += elapsed;
        else
            state.pitch_duration_178 = 0.0f;
    } else
        state.yaw_duration_1c0 = state.pitch_duration_178 = 0.0f;
    if (state.yaw_duration_1c0 > 1.2f || state.pitch_duration_178 > 1.2f) {
        state.yaw_duration_1c0 = state.pitch_duration_178 = 0.0f;
        state.enabled = true;
        if (!state.field_a6) {
            const auto delta = normalize(input.previous_yaw_118 - input.observed_yaw_1ac);
            const std::uint8_t attempt = delta < -30.0f ? 2 : 1;
            state.selection.attempt_by_mode[1] = state.selection.attempt_by_mode[2] = attempt;
            state.selection.status = 0;
        }
    }
    return state.enabled;
}
} // namespace skeet::lagcomp
