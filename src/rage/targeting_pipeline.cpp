#include "skeet/rage/targeting_pipeline.hpp"
#include "targeting_pipeline_detail.hpp"

namespace skeet::rage {
namespace {
void prepare_target_angles(TargetShotChoice choice, FiringState& state, FiringBranchState& branches,
                           client::Command& command, const FiringPostAngleBindings& post) {
    branches.field_33 = post.recoil_option;
    const auto angles =
        target_aim_angles(choice.candidate->primary.position, state.origin, state.recoil,
                          post.compensate_recoil, state.flags, post.angle_math);
    command.angles = {angles.pitch, angles.yaw, angles.roll};
}
TargetFiringResult commit_aimed_target(TargetShotChoice choice, FiringState& state,
                                       FiringBranchState& branches, client::Command& command,
                                       RageShotState& shot, RageShotCommitState& commit,
                                       const FiringReadinessBindings& readiness) {
    shot.flags = state.flags;
    shot.weapon_id = state.weapon_id;
    shot.special_gate_threshold = state.special_threshold;
    shot.marker = state.marker;
    commit.send_packet = *state.send_packet;
    const auto result =
        commit_target_choice(choice, shot, commit, command.movement, readiness.prediction_time,
                             branches.alternate_path, readiness.send_mode);
    state.flags = shot.flags;
    *state.send_packet = commit.send_packet;
    return {result == RageShotCommit::recorded ? TargetFiringStatus::recorded
                                               : TargetFiringStatus::attack_button_only,
            choice};
}
}
TargetFiringResult select_and_fire_target(
    TargetBatch& batch, std::span<const FinalTargetEvaluation> evaluations,
    std::span<const TargetFiringTiming> timing, FiringState& state, FiringBranchState& branches,
    FiringSharedState& shared, client::Command& command, RageShotState& shot,
    RageShotCommitState& commit, const FiringReadinessBindings& readiness,
    const FiringPostAngleBindings& post, float chance_floor, float point_penalty) {
    const auto choice =
        select_target_for_shot(batch, evaluations, commit.context_flags, state.flags,
                               state.weapon_id, chance_floor, point_penalty);
    if (!choice.candidate)
        return {batch.count ? TargetFiringStatus::rejected_batch : TargetFiringStatus::empty_batch,
                choice};
    const auto& selected_timing = timing[choice.ordinal];
    return fire_target_choice(choice, selected_timing, state, branches, shared, command, shot,
                              commit, readiness, post);
}
TargetFiringResult
fire_target_choice(TargetShotChoice choice, const TargetFiringTiming& selected_timing,
                   FiringState& state, FiringBranchState& branches, FiringSharedState& shared,
                   client::Command& command, RageShotState& shot, RageShotCommitState& commit,
                   const FiringReadinessBindings& readiness, const FiringPostAngleBindings& post) {
    const FiringTargetData target{*choice.candidate, selected_timing.field_b4,
                                  selected_timing.field_13c};
    auto current_readiness = readiness;
    current_readiness.scheduler_flags = commit.context_flags;
    const auto ready =
        evaluate_firing_readiness(state, target, branches, shared, command, current_readiness);
    if (ready == FiringReadiness::retained_target)
        return {TargetFiringStatus::retained_target, choice};
    if (ready == FiringReadiness::stopped)
        return {TargetFiringStatus::readiness_stopped, choice};
    return targeting_detail::aim_and_commit_target(choice, state, branches, command, shot, commit,
                                                   readiness, post);
}
TargetFiringResult targeting_detail::aim_and_commit_target(
    TargetShotChoice choice, FiringState& state, FiringBranchState& branches,
    client::Command& command, RageShotState& shot, RageShotCommitState& commit,
    const FiringReadinessBindings& readiness, const FiringPostAngleBindings& post) {
    prepare_target_angles(choice, state, branches, command, post);
    auto current_post = post;
    current_post.prediction_time = readiness.prediction_time;
    current_post.scheduler_flags = commit.context_flags;
    if (!evaluate_firing_post_angles(state, *choice.candidate, branches, command, current_post))
        return {TargetFiringStatus::post_angles_stopped, choice};
    return commit_aimed_target(choice, state, branches, command, shot, commit, readiness);
}
TargetFiringResult targeting_detail::aim_and_commit_target(
    TargetShotChoice choice, FiringState& state, FiringBranchState& branches,
    client::Command& command, RageShotState& shot, RageShotCommitState& commit,
    const FiringReadinessBindings& readiness, const FiringPostAngleBindings& post,
    const animation::BoneRuntime& bones) {
    prepare_target_angles(choice, state, branches, command, post);
    auto current_post = post;
    current_post.prediction_time = readiness.prediction_time;
    current_post.scheduler_flags = commit.context_flags;
    if (!evaluate_firing_post_angles(state, *choice.candidate, branches, command, current_post,
                                     bones))
        return {TargetFiringStatus::post_angles_stopped, choice};
    return commit_aimed_target(choice, state, branches, command, shot, commit, readiness);
}
}
