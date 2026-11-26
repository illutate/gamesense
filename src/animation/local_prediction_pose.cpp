#include "skeet/animation/local_prediction_pose.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include <bit>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void store(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
MovementVector movement_vector(const RageVector& vector) noexcept {
    return {vector[0], vector[1], vector[2]};
}
const client::Command& prediction_command(const client::Command& current, std::uint32_t ordinal,
                                          const LocalPredictionCommandRing& commands) noexcept {
    if (!(ordinal & 1U)) {
        const auto previous = std::uint32_t(current.number) - 1U;
        const auto& command = commands[previous % 150U];
        if (std::uint32_t(command.number) == previous && command.tick != 0x7F7FFFFF)
            return command;
    }
    return current;
}
}
void publish_pose(client::CommandFrame& frame, void* player, const client::Command& command,
                  const LocalPredictionPoseBindings& bindings,
                  LocalPredictionPoseInputs& workspace) {
    auto& scratch = workspace.animation;
    initialize_animation_state_vector(scratch.data());
    lagcomp::AnimationState saved;
    capture_entity_animation(saved, player, bindings.entity.offsets);
    const float saved_yaw = engine::invoke<const RageVector*>(player, 11)->at(1);
    auto* const original_state = field<void*>(player, bindings.entity.offsets.animation_state);
    if (frame.field_1d4 == 1U) {
        if (original_state)
            std::memcpy(scratch.data(), original_state, scratch.size());
        store(player, bindings.entity.offsets.animation_state, static_cast<void*>(scratch.data()));
        const float forward = command.movement.forward_move, side = command.movement.side_move;
        const float square = forward * forward + side * side;
        // Immutable434724A8=10.0f and4346DD40=4.0f.
        if ((command.tick != 0x7F7FFFFF && square > 10.0f) || frame.scalar_c0 > 4.0f) {
            const auto mean = lagcomp::mean_history_timing_ticks(bindings.latency);
            const auto corrected = lagcomp::corrected_command_tick(bindings.entity.command_tick,
                                                                   bindings.entity.previous_command,
                                                                   bindings.entity.current_command);
            const auto mode = std::uint32_t(add(corrected, add(mean, 1)));
            const auto flags = field<std::uint32_t>(player, bindings.offsets.player_flags_e2fbe695);
            const auto velocity = field<RageVector>(player, bindings.offsets.velocity_36375c23);
            const auto origin = *engine::invoke<const RageVector*>(player, 10);
            MovementSimulation simulation;
            auto environment = bindings.movement_environment;
            environment.interval = bindings.interval;
            const anti_aim::MovementCaptureInput movement{bindings.movement_capture.offsets,
                                                          bindings.movement_capture.convars,
                                                          *bindings.interval, &environment};
            initialize_live_movement_simulation(simulation, player, movement_vector(origin),
                                                movement_vector(velocity), flags, mode, movement);
            for (std::uint32_t ordinal = 0; ordinal < 3; ++ordinal) {
                const auto& selected = prediction_command(command, ordinal, bindings.commands);
                auto* const clock = bindings.current_time;
                const float saved_clock = *clock;
                *clock = float(std::bit_cast<std::int32_t>(simulation.mode)) * *bindings.interval;
                environment.interval = bindings.interval;
                simulate_live_movement_tick(simulation, bindings.collision, environment, player,
                                            selected);
                const auto saved_velocity =
                    field<RageVector>(player, bindings.offsets.velocity_36375c23);
                store(player, bindings.offsets.velocity_36375c23, simulation.velocity);
                const auto saved_duck =
                    field<std::uint32_t>(player, bindings.offsets.duck_3309c0cf);
                store(player, bindings.offsets.duck_3309c0cf, simulation.duck_amount);
                const auto saved_flags =
                    field<std::uint32_t>(player, bindings.offsets.player_flags_e2fbe695);
                store(player, bindings.offsets.player_flags_e2fbe695, simulation.player_flags);
                const auto saved_command =
                    field<std::uint32_t>(player, bindings.offsets.command_f58e6118);
                store(player, bindings.offsets.command_f58e6118,
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(&selected)));
                const auto saved_origin =
                    field<RageVector>(player, bindings.offsets.origin_4e66bb34);
                bindings.animation_update_flag = 1;
                store(player, bindings.offsets.origin_4e66bb34, simulation.position);
                const auto saved_absolute =
                    field<RageVector>(player, bindings.offsets.origin_2a671b42);
                store(player, bindings.offsets.origin_2a671b42, simulation.position);
                const auto saved_body =
                    field<std::uint32_t>(player, bindings.offsets.body_yaw_a2a8247f);
                store(player, bindings.offsets.body_yaw_a2a8247f, selected.angles[1]);
                const AnimationUpdateTiming timing{*bindings.animation_frame,
                                                   *bindings.current_time, *bindings.interval,
                                                   nullptr, &bindings.animation_reset};
                const auto animation_engine =
                    lagcomp::native_history_animation_engine(bindings.animation_update);
                update_animation_state(scratch.data(), selected.angles, timing, animation_engine);
                store(player, bindings.offsets.body_yaw_a2a8247f, saved_body);
                store(player, bindings.offsets.origin_2a671b42, saved_absolute[0]);
                bindings.animation_update_flag = 0;
                store(player, bindings.offsets.origin_2a671b42 + 4U, saved_absolute[1]);
                store(player, bindings.offsets.origin_2a671b42 + 8U, saved_absolute[2]);
                store(player, bindings.offsets.origin_4e66bb34, saved_origin);
                store(player, bindings.offsets.command_f58e6118, saved_command);
                store(player, bindings.offsets.player_flags_e2fbe695, saved_flags);
                store(player, bindings.offsets.duck_3309c0cf, saved_duck);
                store(player, bindings.offsets.velocity_36375c23, saved_velocity);
                *clock = saved_clock; //4331F1E3 reloads its captured pointer, not43467EB8
            }
        }
    }
    const auto angles = *engine::invoke<const RageVector*>(player, 11);
    const BoneRequest request{angles, frame.origin_10c, 0.0f, 0x300U, -1, 1, false};
    std::int32_t bone_count = 256;
    if (bindings.bones.build_bone_matrices(player, request, workspace.bones, &bone_count)) {
        bindings.output.timestamp = *bindings.publication_clock;
        bindings.bones.select_hitbox_bone_matrices(player, workspace.bones,
                                                   bindings.output.hitboxes);
        bindings.output.origin = frame.origin_10c;
    }
    restore_entity_animation(saved, player, bindings.entity.offsets);
    if (bindings.set_absolute_angles) {
        const RageVector restored{0.0f, saved_yaw, 0.0f};
        bindings.set_absolute_angles(player, restored);
    }
    store(player, bindings.entity.offsets.animation_state, original_state);
    release_animation_state_vector(scratch.data(), bindings.allocator);
}
void publish_local_prediction_pose(client::CommandFrame& frame, void* player,
                                   const client::Command& command, std::uint8_t send_packet,
                                   const LocalPredictionPoseBindings& bindings) {
    if (!send_packet && frame.choked_15c < frame.choke_limit_fc)
        return;
    //4331EE04 reserves these locals itself. Only the five vector words are
    // initialized unconditionally; mode1 copies348 bytes when state exists.
    // The native null-state branch retains its own unwritten stack bytes.
    // Bone matrices are output of43388720, never bootstrap input.
    LocalPredictionPoseInputs workspace;
    publish_pose(frame, player, command, bindings, workspace);
}
void publish_local_prediction_pose_with_inputs(client::CommandFrame& frame, void* player,
                                               const client::Command& command,
                                               std::uint8_t send_packet,
                                               const LocalPredictionPoseBindings& bindings,
                                               const LocalPredictionPoseInputs& incoming) {
    if (!send_packet && frame.choked_15c < frame.choke_limit_fc)
        return;
    LocalPredictionPoseInputs workspace;
    std::memcpy(&workspace, &incoming, sizeof workspace);
    publish_pose(frame, player, command, bindings, workspace);
}
} // namespace skeet::animation
