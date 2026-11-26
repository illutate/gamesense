#include "skeet/anti_aim/finisher.hpp"
#include <cstring>

namespace skeet::anti_aim {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool model_ready(const animation::BoneModel& model) noexcept {
    return model.renderable_model && model.model_index != -1 && model.studio_header &&
           (!model.virtual_model_required || model.virtual_model);
}
bool build_finisher_bones(FinisherState& state, const animation::BoneRequest& request,
                          const FinisherBindings& bindings) {
    state.common.value_1b0 = 0;
    if (model_ready(bindings.model))
        state.common.value_1b0 = static_cast<std::uint32_t>(bindings.model.parents.size());
    return animation::build_bone_matrices(bindings.bone_state, bindings.model, request,
                                          state.matrices_520, bindings.bones,
                                          bindings.trigonometry);
}
bool build_finisher_bones(FinisherState& state, const animation::BoneRequest& request,
                          const LiveFinisherBindings& bindings) {
    // Corresponding signed/unsigned integer types may alias. The canonical
    // count is published before pose accumulation, as the native owner does.
    return bindings.bones.build_bone_matrices(
        bindings.player, request, state.matrices_520,
        reinterpret_cast<std::int32_t*>(&state.common.value_1b0));
}
}
template <class Bindings>
void finish_angles(bool enabled, std::int32_t body_yaw, FinisherState& state,
                   const FinisherPlayer& player, std::int32_t frame_command,
                   const client::Command& command, std::uint8_t send_packet,
                   const Bindings& bindings) {
    if (!enabled || body_yaw == 0 || !send_packet || !bindings.animation_state)
        return;
    auto* const original_state = bindings.animation_state;
    if ((player.flags_38ca5228 & 8u) || player.field_043fa575 != player.field_334b460c ||
        read<float>(original_state, 0x6C) == player.field_86a5f8b3_minus4 ||
        read<std::uint32_t>(state.animation_1d8.data(), 0x60) != player.identity) {
        std::memcpy(state.animation_1d8.data(), original_state, 0x348);
        std::memset(state.animation_1d8.data() + 0x13C, 0, 0x14);
        state.common.value_1b4 = -1;
    }
    bindings.animation_state = state.animation_1d8.data();
    const auto scalar = bindings.scalar_a2a8247f;
    bindings.scalar_a2a8247f = bindings.simulation_time;
    lagcomp::AnimationState snapshot;
    animation::capture_entity_animation(snapshot, bindings.player, bindings.entity_offsets);
    const auto absolute = *engine::invoke<const RageVector*>(bindings.player, 11);
    auto angles = command.angles;
    animation::select_animation_angles(bindings.angle_history, frame_command, angles);
    animation::update_animation_state(state.animation_1d8.data(), angles, bindings.timing,
                                      bindings.update);
    if (*bindings.build_bones_12c) {
        state.absolute_angles_1b8 = *engine::invoke<const RageVector*>(bindings.player, 11);
        const auto saved_eye = bindings.eye_angles;
        bindings.eye_angles = bindings.final_angles;
        // Native43388720 initializes the optional count before its model gate,
        // then publishes the studio count before engine pose accumulation.
        const animation::BoneRequest request{
            state.absolute_angles_1b8, {0.0f, 0.0f, 0.0f}, 0.0f, 0xFFF00u, -1, 0, true};
        if (build_finisher_bones(state, request, bindings))
            state.common.value_1b4 = frame_command;
        bindings.eye_angles = saved_eye;
    }
    animation::restore_entity_animation(snapshot, bindings.player, bindings.entity_offsets);
    if (bindings.set_absolute_angles)
        bindings.set_absolute_angles(bindings.player, absolute);
    bindings.scalar_a2a8247f = scalar;
    bindings.animation_state = original_state;
    state.common.reference_yaw_258 = read<float>(state.animation_1d8.data(), 0x80);
}
void finish_command_angles(bool enabled, std::int32_t body_yaw, FinisherState& state,
                           const FinisherPlayer& player, std::int32_t frame_command,
                           const client::Command& command, std::uint8_t send_packet,
                           const FinisherBindings& bindings) {
    finish_angles(enabled, body_yaw, state, player, frame_command, command, send_packet, bindings);
}
void finish_command_angles(bool enabled, std::int32_t body_yaw, FinisherState& state,
                           const FinisherPlayer& player, std::int32_t frame_command,
                           const client::Command& command, std::uint8_t send_packet,
                           const LiveFinisherBindings& bindings) {
    finish_angles(enabled, body_yaw, state, player, frame_command, command, send_packet, bindings);
}
} // namespace skeet::anti_aim
