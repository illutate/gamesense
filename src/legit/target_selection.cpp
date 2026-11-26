#include "skeet/legit/target_selection.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <cmath>
#include <immintrin.h>
#include <stdexcept>

namespace skeet::legit {
namespace {
AimTarget target_angles(std::int32_t index, void* entity, RuntimeState& state,
                        const TargetSelectionInput& input,
                        const TargetSelectionBindings& bindings) {
    const auto& track = state.targets[std::uint32_t(index) & 63u];
    Vector point{track.point[0] + state.target_offset[0], state.target_offset[1] + track.point[1],
                 state.target_offset[2] + track.point[2]};
    predict_target_point(state.profile, bindings.tracking, entity, point);
    auto x = point[0] - input.eye_position[0], y = point[1] - input.eye_position[1],
         z = point[2] - input.eye_position[2];
    const auto distance = std::sqrt((x * x + y * y) + z * z);
    const auto inverse = 1.0f / (distance + 1.1920928955078125e-7f);
    x = inverse * x;
    y = inverse * y;
    z = inverse * z;
    Vector angles{};
    if (y == 0.0f && x == 0.0f)
        angles[0] = z <= 0.0f ? 90.0f : 270.0f;
    else {
        angles[1] = float(bindings.arc_tangent2(double(y), double(x))) * 57.2957763671875f;
        const auto horizontal = std::sqrt(x * x + y * y);
        angles[0] =
            float(bindings.arc_tangent2(double(-z), double(horizontal))) * 57.2957763671875f;
    }
    const auto pitch = normalize_movement_angle(angles[0] - input.reference_angles[0]);
    const auto yaw = normalize_movement_angle(angles[1] - input.reference_angles[1]);
    return {index, angles, std::sqrt(pitch * pitch + yaw * yaw), distance};
}
bool target_ready(const ProfileState& profile, const TargetTrack& track) {
    return !(track.elapsed < profile.reaction_time) &&
           bool(profile.aim_hitgroups & track.visible_hitgroups);
}
}
AimTarget select_aim_target(RuntimeState& state, const TargetSelectionInput& input,
                            const TargetSelectionBindings& bindings) {
    if (!bindings.arc_tangent2)
        throw std::invalid_argument("LEGIT selection requires native atan2 binding");
    AimTarget best;
    best.field_of_view = state.profile.maximum_fov;
    const auto release = [&] {
        release_target(state,
                       bindings.tracking.live_interval ? *bindings.tracking.live_interval
                                                       : bindings.tracking.interval,
                       bindings.random);
    };
    for (std::int32_t index = 1; index <= input.maximum_clients; ++index) {
        auto* entity = engine::invoke<void*>(bindings.entities.entity_list, 3, index);
        if (!target_ready(state.profile, state.targets[std::uint32_t(index) & 63u]))
            continue;
        auto candidate = target_angles(index, entity, state, input, bindings);
        if (candidate.distance == 0.0f)
            continue;
        const auto pitch =
            normalize_movement_angle(candidate.angles[0] - input.reference_angles[0]);
        const auto yaw = normalize_movement_angle(candidate.angles[1] - input.reference_angles[1]);
        const auto maximum = (1000.0f / candidate.distance) * state.profile.maximum_fov;
        if (maximum < std::fabs(pitch) || maximum < std::fabs(yaw) ||
            best.field_of_view <= candidate.field_of_view)
            continue;
        best = candidate;
    }
    const auto previous = state.target;
    if (previous) {
        auto* entity = engine::invoke<void*>(bindings.entities.entity_list, 3, previous);
        const auto& retained_track = state.targets[std::uint32_t(previous) & 63u];
        if (!rage::target_is_eligible(bindings.entities, entity) ||
            retained_track.elapsed < state.profile.reaction_time ||
            !(retained_track.visible_hitgroups & state.profile.aim_hitgroups)) {
            state.block_until = (input.live_time ? *input.live_time : input.time) + 0.4f;
            release();
            return {};
        }
        if (best.index != previous) {
            auto retained = target_angles(previous, entity, state, input, bindings);
            if (state.profile.maximum_fov <= retained.field_of_view ||
                2.0f < retained.field_of_view - best.field_of_view) {
                release();
                return {};
            }
            best = retained;
        }
    }
    if (best.index <= 0) {
        release();
        return {};
    }
    if (previous != best.index) {
        if (!bindings.random.seed || !bindings.random.uniform)
            throw std::invalid_argument(
                "LEGIT target change requires native RandomSeed/RandomFloat");
        state.target = best.index;
        if (best.index != state.previous_target) {
            state.target_offset[0] = bindings.random.uniform(bindings.random.context, -2.0f, 2.0f);
            state.target_offset[1] = bindings.random.uniform(bindings.random.context, -2.0f, 2.0f);
            state.target_offset[2] = bindings.random.uniform(bindings.random.context, -1.0f, 1.0f);
            state.previous_target = state.target;
        }
        const auto tick_count = _mm_cvttss_si32(
            _mm_set_ss(1.0f / (bindings.tracking.live_interval ? *bindings.tracking.live_interval
                                                               : bindings.tracking.interval)));
        if (!tick_count)
            throw std::domain_error("Native target selection interval has no tick divisor");
        bindings.random.seed(bindings.random.context, state.last_command / tick_count);
        state.curve = bindings.random.uniform(bindings.random.context, -1.5f, 1.5f);
    }
    return best;
}
} // namespace skeet::legit
