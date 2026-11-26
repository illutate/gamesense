#include "skeet/movement/z_hop.hpp"
#include <cmath>

namespace skeet {

bool apply_z_hop_heading(const ZHopSettings& settings, InputBitmap input, float engine_time,
                         MovementAngleView movement_angles) {
    if (!settings.enabled || !hotkey_active(settings.hotkey, input))
        return false;
    const float amplitude = static_cast<float>(settings.amplitude) * 0.1f;
    float phase = static_cast<float>(settings.frequency) * 0.1f;
    phase *= engine_time;
    movement_angles.yaw += std::sin(phase) * amplitude;
    return true;
}

} // namespace skeet
