#include "skeet/lagcomp/history_environment.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/resolver.hpp"
#include "skeet/engine/opaque_native_word.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::lagcomp {
namespace {
struct RestoreSuppression {
    std::uint8_t* flag;
    std::uint8_t saved = 0;
    explicit RestoreSuppression(std::uint8_t* value) : flag(value) {
        if (flag) {
            saved = *flag;
            *flag = 0;
        }
    }
    ~RestoreSuppression() {
        if (flag)
            *flag = saved;
    }
};
bool large_yaw_change(float difference) noexcept {
    // The native NaN check bypasses normalization and the timestamp write.
    if (std::isnan(difference))
        return false;
    if (std::fabs(difference) > 180.0f) {
        const float turns = round_half_away_from_zero(std::fabs(difference / 360.0f)) * 360.0f;
        difference = difference < 0.0f ? difference + turns : difference - turns;
    }
    return std::fabs(difference) > 45.0f;
}
}

HistoryEnvironmentUpdate update_history_environment(client::PlayerState& owner, const void* target,
                                                    HistoryEnvironmentScratch& scratch,
                                                    const HistoryEnvironmentInput& input,
                                                    const HistoryEnvironmentEngine& engine) {
    RageVector target_origin;
    std::memcpy(&target_origin, static_cast<const std::byte*>(target) + engine.origin_offset,
                sizeof(target_origin));
    // Native433AC5BD remains active through the angle import and timestamp
    // policy. The concrete probe's nested guards restore this zero, not the
    // caller's original suppression byte, until4341873D at the end.
    RestoreSuppression suppression(engine.probe.suppression_flag);
    const auto probe =
        resolver_runtime::probe_environment(input.local_origin, target_origin, engine.probe);
    auto& state = owner.native;
    HistoryEnvironmentUpdate result{probe.selected, false};
    if (!probe.selected) {
        state.environment_yaw = std::numeric_limits<float>::max();
        return result;
    }

    RageVector direction = probe.direction;
    if (probe.damage[0] == 0 && probe.damage[1] == 0) {
        //4340CEAF replaced stack+1F0 with the native ring address; stack+1F4
        // retains the preceding direction.x.4340E366 reuses those two words.
        // This deliberately differs from resolver43348FFC's local-eye reuse.
        direction[0] = engine::read_opaque_native_float(scratch.direction.data());
        direction[1] = std::bit_cast<float>(input.native_history_address);
    } else {
        scratch.direction[0] = direction[0];
        scratch.direction[1] = direction[1];
        // The sign-flipped side writes only x/y at4340E301..E31C.
        if (probe.damage[1] != 0)
            scratch.direction[2] = direction[2];
    }
    float yaw = 0.0f;
    if (direction[0] != 0.0f || direction[1] != 0.0f) {
        // Native converts atan2's double result to float before multiplying.
        yaw = static_cast<float>(engine.atan2(double(direction[1]), double(direction[0]))) *
              57.2957763671875f;
    }
    state.environment_yaw = yaw;
    if (yaw == std::numeric_limits<float>::max() || !(state.field_51c < 0.75f) ||
        owner.history.count < 2u || input.simulation_ticks > 2)
        return result;
    const auto& newest = owner.history.at(0).header;
    const float newest_yaw = std::bit_cast<float>(newest.player_history_1ac);
    if (!large_yaw_change(state.reference_yaw - newest_yaw))
        return result;
    const auto& previous = owner.history.at(1).header;
    const float previous_yaw = std::bit_cast<float>(previous.player_history_1ac);
    if (large_yaw_change(newest_yaw - previous_yaw)) {
        state.environment_yaw_change_time = input.current_time;
        result.timestamp_updated = true;
    }
    return result;
}
} // namespace skeet::lagcomp
