#include "skeet/resolver_runtime/build.hpp"
#include "skeet/resolver_runtime/build_validation.hpp"
#include "skeet/animation/bone_runtime.hpp"
#include "skeet/animation/record_pose_scope.hpp"
#include "skeet/lagcomp/records.hpp"
#include <bit>
#include <cstring>

namespace skeet::resolver_runtime {
namespace {
class RestoreLivePoses {
public:
    explicit RestoreLivePoses(std::span<float, 24> destination) noexcept : poses(destination) {
        std::memcpy(saved.data(), poses.data(), sizeof saved);
    }
    ~RestoreLivePoses() {
        std::memcpy(poses.data(), saved.data(), sizeof saved);
    }
    std::span<float, 24> poses;

private:
    std::array<std::uint32_t, 24> saved;
};
}
std::uint32_t build_hypotheses(lagcomp::LagRecord& record, const HypothesisInput& input,
                               void* player, const animation::BoneRuntime& runtime, bool strict) {
    animation::RecordPoseScope record_pose(player, record, runtime, strict);
    if (strict)
        record.header.flags &= ~0x80u;
    if (!record_pose.valid() || (strict && !valid_hypothesis_input(input)))
        return 0;
    RestoreLivePoses restore(runtime.pose_parameters(player));
    auto parameters = input;
    parameters.record_flags = record.header.flags;
    if (strict) {
        parameters.velocity_x = record.header.velocity[0];
        parameters.velocity_y = record.header.velocity[1];
        parameters.velocity_z = record.header.velocity[2];
        parameters.duck_amount = record.header.duck_amount;
        parameters.feet_yaw = record.header.animation_yaw;
        parameters.lower_body_yaw = record.header.field_a2a8247f;
        parameters.movement_yaw = record.animation.pose_parameters[7] * 360.0f - 180.0f;
    }
    const auto hypotheses = solve_hypotheses(parameters);
    std::uint32_t completed = 0;
    for (std::size_t index = 0; index != record.alternatives.size(); ++index) {
        auto& alternative = record.alternatives[index];
        const auto& hypothesis = hypotheses[index];
        if (strict && !valid_hypothesis(hypothesis))
            return completed;
        alternative.state[0] = std::bit_cast<std::uint32_t>(hypothesis.yaw);
        alternative.state[1] = std::bit_cast<std::uint32_t>(hypothesis.pose_yaw);
        restore.poses[11] = hypothesis.body_pose_parameter;
        restore.poses[7] = hypothesis.move_pose_parameter;
        std::int32_t count = 0;
        if (strict) {
            std::array<Matrix3x4, animation::maximum_bones> matrices;
            const animation::BoneRequest request{
                {0.0f, hypothesis.yaw, 0.0f}, record.header.origin, 0.0f, 0x80300u, -1, 0, false};
            if (runtime.build_bone_matrices(player, request, matrices))
                count = runtime.select_hitbox_bone_matrices(player, matrices, alternative.matrices);
        } else
            count = runtime.build_resolver_hitbox_matrices(player, hypothesis.yaw,
                                                           alternative.matrices);
        if (count <= 0 || (strict && !complete_hitbox_matrices(count, alternative.matrices)))
            return completed;
        ++completed;
    }
    record.header.flags |= 0x80u;
    return completed;
}
} // namespace skeet::resolver_runtime
