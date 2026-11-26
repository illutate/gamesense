#include "skeet/resolver_runtime/build.hpp"
#include "skeet/resolver_runtime/build_validation.hpp"
#include "skeet/lagcomp/records.hpp"
#include <bit>
#include <cstring>

namespace skeet::resolver_runtime {
namespace {
struct RestorePoses {
    std::span<float, 24> destination;
    std::array<float, 24> saved;
    explicit RestorePoses(std::span<float, 24> poses) : destination(poses) {
        std::memcpy(saved.data(), poses.data(), sizeof(saved));
    }
    ~RestorePoses() {
        std::memcpy(destination.data(), saved.data(), sizeof(saved));
    }
};
}

std::uint32_t build_hypotheses(ResolverRecord& record, const HypothesisInput& input,
                               HypothesisBuildContext& context) {
    RestorePoses restore(context.pose_parameters);

    if (context.strict)
        record.state_flags &= ~0x80u;
    if (context.strict && !valid_hypothesis_input(input))
        return 0;

    auto parameters = input;
    parameters.record_flags = record.state_flags;
    const auto hypotheses = solve_hypotheses(parameters);
    const std::array<ResolverVariant*, 3> variants = {&record.first, &record.center, &record.third};
    std::uint32_t completed = 0;
    for (std::size_t index = 0; index != variants.size(); ++index) {
        auto& variant = *variants[index];
        const auto& hypothesis = hypotheses[index];
        if (context.strict && !valid_hypothesis(hypothesis))
            return completed;
        variant.yaw = hypothesis.yaw;
        variant.pose_yaw = hypothesis.pose_yaw;
        context.pose_parameters[11] = hypothesis.body_pose_parameter;
        context.pose_parameters[7] = hypothesis.move_pose_parameter;
        const auto count = animation::build_resolver_hitbox_matrices(
            context.entity, context.model, context.origin, variant.yaw, variant.matrices,
            context.engine, context.trigonometry);
        if (count <= 0 || (context.strict && !complete_hitbox_matrices(count, variant.matrices)))
            return completed;
        ++completed;
    }
    record.state_flags |= 0x80U;
    return completed;
}

std::uint32_t build_hypotheses(lagcomp::LagRecord& record, const HypothesisInput& input,
                               HypothesisBuildContext& context) {
    RestorePoses restore(context.pose_parameters);
    if (context.strict)
        record.header.flags &= ~0x80u;
    if (context.strict && !valid_hypothesis_input(input))
        return 0;
    auto parameters = input;
    parameters.record_flags = record.header.flags;
    const auto hypotheses = solve_hypotheses(parameters);
    std::uint32_t completed = 0;
    for (std::size_t index = 0; index != record.alternatives.size(); ++index) {
        auto& variant = record.alternatives[index];
        const auto& hypothesis = hypotheses[index];
        if (context.strict && !valid_hypothesis(hypothesis))
            return completed;
        variant.state[0] = std::bit_cast<std::uint32_t>(hypothesis.yaw);
        variant.state[1] = std::bit_cast<std::uint32_t>(hypothesis.pose_yaw);
        context.pose_parameters[11] = hypothesis.body_pose_parameter;
        context.pose_parameters[7] = hypothesis.move_pose_parameter;
        const auto count = animation::build_resolver_hitbox_matrices(
            context.entity, context.model, context.origin, hypothesis.yaw, variant.matrices,
            context.engine, context.trigonometry);
        if (count <= 0 || (context.strict && !complete_hitbox_matrices(count, variant.matrices)))
            return completed;
        ++completed;
    }
    record.header.flags |= 0x80U;
    return completed;
}

} // namespace skeet::resolver_runtime
