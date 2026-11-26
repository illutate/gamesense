#include "skeet/rage/firing.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/animation/bone_runtime.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
MovementAngles angles(const client::Command& command) noexcept {
    return {command.angles[0], command.angles[1], command.angles[2]};
}
void angles(client::Command& command, const MovementAngles& value) noexcept {
    command.angles = {value.pitch, value.yaw, value.roll};
}
const MovementAngles* reference_angles(const FiringState& state, const client::Command& command,
                                       const FiringPostAngleBindings& bindings,
                                       MovementAngles& saved) {
    const auto player = bindings.player;
    const auto& offsets = bindings.offsets;
    if (!(field<std::uint32_t>(player, offsets.flags) & 8U) &&
        field<std::uint8_t>(player, offsets.first_byte) ==
            field<std::uint8_t>(player, offsets.second_byte)) {
        const auto previous = engine::invoke<const client::Command*>(
            bindings.input, 8, 0, std::bit_cast<std::int32_t>(std::uint32_t(command.number) - 1U));
        if (previous) {
            saved = angles(*previous);
            return &saved;
        }
    }
    return &state.reference_angles;
}
const void* studio_wrapper(const FiringPostAngleBindings& bindings) noexcept {
    const auto wrapper = field<const void*>(bindings.player, bindings.offsets.studio_header);
    return wrapper && field<const void*>(wrapper, 0) ? wrapper : nullptr;
}
struct SemanticBoneBuild {
    const FiringPostAngleBindings& bindings;
    bool build(void*, const animation::BoneRequest& request, std::span<Matrix3x4> matrices) const {
        return animation::build_bone_matrices(
            bindings.bone_state.decoded(), bindings.bone_model.decoded(), request, matrices,
            bindings.bone_engine.decoded(), bindings.trigonometry);
    }
};
struct LiveBoneBuild {
    const animation::BoneRuntime& runtime;
    bool build(void* player, const animation::BoneRequest& request,
               std::span<Matrix3x4> matrices) const {
        std::int32_t count = 0;
        return runtime.build_bone_matrices(player, request, matrices, &count);
    }
};
}
bool limit_firing_angle_step(MovementAngles& aim, const MovementAngles& reference,
                             float maximum_step) noexcept {
    aim.roll = 0;
    auto pitch = normalize_movement_angle(aim.pitch - reference.pitch);
    auto yaw = normalize_movement_angle(aim.yaw - reference.yaw);
    const auto distance = std::sqrt(pitch * pitch + yaw * yaw);
    if (maximum_step > distance)
        return false;
    const auto scale = maximum_step / distance;
    pitch *= scale;
    yaw *= scale;
    aim.pitch = pitch + reference.pitch;
    aim.yaw = yaw + reference.yaw;
    return true;
}
bool firing_animation_ready(const void* animation, std::uint8_t guard) noexcept {
    return field<std::uint8_t>(animation, 0x109) != 0 || field<float>(animation, 0xa4) != 0.0f ||
           !(guard & 1U);
}
float correct_firing_eye_height(const Matrix3x4& bone, float height) noexcept {
    const auto ceiling = bone.rows[2][3] + 1.7f;
    if (!(height > ceiling))
        return height;
    const auto raw = (std::fabs(height - ceiling) - 4.0f) / 6.0f;
    auto t = raw > 0.0f ? raw : 0.0f;
    t = t < 1.0f ? t : 1.0f;
    const auto square = t * t;
    const auto weight = (square * 3.0f - (square + square) * t) + 0.0f;
    return (ceiling - height) * weight + height;
}
template <class BoneBuild>
bool evaluate_post_angles(FiringState& state, const TargetCandidate& target,
                          FiringBranchState& branch, client::Command& command,
                          const FiringPostAngleBindings& bindings, const BoneBuild& bones) {
    const auto limit = [&] {
        MovementAngles saved;
        const auto reference = reference_angles(state, command, bindings, saved);
        auto aim = angles(command);
        const bool limited = limit_firing_angle_step(aim, *reference, state.angle_step);
        angles(command, aim);
        return limited;
    };
    if ((state.flags & 0x40U) && limit()) {
        if (state.weapon_id != 64 || state.special_threshold >= bindings.prediction_time)
            return false;
    }
    if (!bindings.enabled)
        return false;
    if (bindings.scheduler_flags & 9U)
        return true;
    branch.field_22 = true;
    if (!bindings.send_mode || (state.flags & 0x400000U))
        return true;
    const auto player = bindings.player;
    const auto& offsets = bindings.offsets;
    const auto animation_data = field<const void*>(player, offsets.animation_state);
    if (!animation_data)
        return true;
    if (!firing_animation_ready(animation_data,
                                field<std::uint8_t>(player, offsets.animation_guard)))
        return true;
    // Both original433FD30D calls are simple decoded-field reads.
    if (!studio_wrapper(bindings))
        return true;
    const auto wrapper = studio_wrapper(bindings);
    if (!wrapper)
        return true;
    const auto header = field<const std::byte*>(wrapper, 0);
    const auto set_index = field<std::int32_t>(player, offsets.hitbox_set);
    const auto set = header + field<std::int32_t>(header, 0xb0) + std::ptrdiff_t(set_index) * 12;
    if (!field<std::int32_t>(set, 4))
        return true;
    const auto boxes = set + field<std::int32_t>(set, 8);
    const auto bone_index = field<std::int32_t>(boxes, 0);
    if (bone_index == -1)
        return true;
    auto pitch = command.angles[0];
    if (!(state.flags & 0x10U))
        pitch -= state.recoil[0];
    pitch = normalize_movement_angle(pitch) + state.field_a8;
    if (pitch > 180.0f)
        pitch -= 360.0f;
    pitch = _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(pitch), _mm_set_ss(-90.0f)), _mm_set_ss(90.0f)));
    auto* pose = static_cast<std::byte*>(player) + offsets.pose_parameters + 0x30;
    const auto saved_pose = field<float>(pose, 0);
    if (state.origin[2] == state.bone_origin[2] + state.field_164 &&
        pitch <= saved_pose * 180.0f - 90.0f)
        return true;
    float normalized = pitch;
    bindings.convert_pose_parameter(wrapper, 12, pitch, normalized);
    write(pose, 0, normalized);
    std::array<Matrix3x4, animation::maximum_bones> matrices;
    const animation::BoneRequest request{{0, field<float>(animation_data, 0x80), 0},
                                         state.bone_origin,
                                         bindings.prediction_time,
                                         0x300U,
                                         bone_index,
                                         0,
                                         true};
    const auto built = bones.build(player, request, matrices);
    if (built) {
        const auto height = correct_firing_eye_height(matrices[std::size_t(bone_index)],
                                                      state.bone_origin[2] + state.field_164);
        if (height != state.origin[2]) {
            state.field_84 = height - state.bone_origin[2];
            state.origin[2] = height;
            branch.field_33 = bindings.recoil_option;
            const auto aim =
                target_aim_angles(target.primary.position, state.origin, state.recoil,
                                  bindings.compensate_recoil, state.flags, bindings.angle_math);
            angles(command, aim);
            branch.field_22 = true;
            if ((state.flags & 0x40U) && limit() &&
                (state.weapon_id != 64 || state.special_threshold >= bindings.prediction_time))
                branch.field_22 = false;
        }
    }
    write(pose, 0, saved_pose);
    return branch.field_22;
}
bool evaluate_firing_post_angles(FiringState& state, const TargetCandidate& target,
                                 FiringBranchState& branch, client::Command& command,
                                 const FiringPostAngleBindings& bindings) {
    return evaluate_post_angles(state, target, branch, command, bindings,
                                SemanticBoneBuild{bindings});
}
bool evaluate_firing_post_angles(FiringState& state, const TargetCandidate& target,
                                 FiringBranchState& branch, client::Command& command,
                                 const FiringPostAngleBindings& bindings,
                                 const animation::BoneRuntime& runtime) {
    return evaluate_post_angles(state, target, branch, command, bindings, LiveBoneBuild{runtime});
}
} // namespace skeet::rage
