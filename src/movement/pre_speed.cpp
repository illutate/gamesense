#include "skeet/movement/pre_speed.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;

PreSpeedTurn prepare_pre_speed_turn(PreSpeedLimits limits, float interval_per_tick,
                                    float player_speed_limit, MovementVector velocity) noexcept {
    const float speed = horizontal_length(velocity);
    const float tick_scale = interval_per_tick * 128.0f;
    const float minimum_step = tick_scale * 2.25f;
    const float maximum_step = tick_scale * 5.0f;
    const float angle =
        minimum_number(limits.maximum_angle, limits.angle_scale / (speed + epsilon)) * 2.0f;
    const float turn = sse_min(sse_max(angle, minimum_step), maximum_step);
    return {speed, minimum_step, maximum_step, turn, player_speed_limit >= speed};
}

} // namespace skeet
