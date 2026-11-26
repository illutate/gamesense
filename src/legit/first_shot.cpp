#include "skeet/legit/runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cmath>
#include <limits>

namespace skeet::legit {
void apply_first_shot_delay(const Settings& settings, ProfileState& state,
                            const FirstShotFrame& frame, const rage::TargetBindings& bindings,
                            InputBitmap input, std::uint32_t& buttons) {
    const auto threshold = state.first_shot_range;
    if (threshold == 0.0f || !(buttons & 1u) || !state.enabled ||
        !hotkey_active(settings.aim_keys[std::size_t(state.profile)].value.binding, input)) {
        state.secondary_flags &= 0xF7u;
        state.previous_buttons = frame.prediction_buttons;
        return;
    }
    if (state.shots != 0)
        state.secondary_flags &= 0xF7u;
    else if (!(state.previous_buttons & 1u)) {
        if (frame.prediction_buttons & 1u)
            state.secondary_flags |= 8u;
        else
            state.secondary_flags &= 0xF7u;
    }
    state.previous_buttons = frame.prediction_buttons;
    if (!(state.secondary_flags & 8u))
        return;
    for (std::int32_t index = 1; index <= frame.maximum_clients; ++index) {
        auto* target = engine::invoke<void*>(bindings.entity_list, 3, index);
        if (!rage::target_is_eligible(bindings, target))
            continue;
        const auto origin = *engine::invoke<const Vector*>(target, 10);
        const auto lower = *engine::invoke<const Vector*>(target, 76);
        const auto upper = *engine::invoke<const Vector*>(target, 77);
        const auto height = upper[2] + lower[2];
        const auto& eye = frame.eye_position;
        const auto& forward = frame.forward;
        const auto horizontal =
            (origin[1] - eye[1]) * forward[1] + (origin[0] - eye[0]) * forward[0];
        for (const auto fraction : {0.55f, 0.75f, 0.9f}) {
            const auto z = fraction * height + origin[2];
            const auto projection = (z - eye[2]) * forward[2] + horizontal;
            float distance;
            if (projection < 0.0f || projection > 10000.0f)
                distance = std::numeric_limits<float>::max();
            else {
                const auto x_delta = origin[0] - (forward[0] * projection + eye[0]);
                const auto y_delta = origin[1] - (forward[1] * projection + eye[1]);
                const auto z_delta = z - (forward[2] * projection + eye[2]);
                distance = std::sqrt((x_delta * x_delta + y_delta * y_delta) + z_delta * z_delta);
            }
            if (threshold > distance)
                return;
        }
    }
    buttons &= 0xFFFFFFFEu;
}
} // namespace skeet::legit
