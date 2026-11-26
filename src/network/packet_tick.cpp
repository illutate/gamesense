#include "skeet/network/packet_scheduler.hpp"
#include <bit>

namespace skeet::network {
void schedule_fake_lag(FakeLagState& state, const FakeLagTriggerFrame& frame,
                       const FakeLagActivation& activation, bool freeze_period,
                       anti_aim::TargetState& target, const anti_aim::TargetRefreshContext& refresh,
                       FakeLagTriggerEnvironment& environment, const RandomInterface& random,
                       std::uint32_t& flags, std::uint8_t& send_packet, bool& collision_result) {
    begin_fake_lag_tick(state, frame.packet, collision_result);
    if (freeze_period)
        return;
    if (fake_lag_active(activation)) {
        if (select_fake_lag_limit(state, frame.packet, environment.interval, random)) {
            anti_aim::refresh_yaw_target(target, refresh);
            apply_fake_lag_triggers(state, frame, target, environment, flags, collision_result);
        }
        apply_fake_lag_send_gate(state, frame.packet, flags, send_packet);
    }
    finish_fake_lag_tick(state, frame.packet, target.handle,
                         std::bit_cast<std::int32_t>(target.fields_18[0]));
}
} // namespace skeet::network
