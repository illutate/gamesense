#include "skeet/legit/command.hpp"
#include "skeet/legit/trace_filter.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t difference(std::int32_t first, std::int32_t second) {
    auto value = std::uint32_t(first) - std::uint32_t(second);
    const auto sign = std::bit_cast<std::int32_t>(value) < 0 ? 0xffffffffu : 0u;
    return std::bit_cast<std::int32_t>((value ^ sign) - sign);
}
bool convar_enabled(const void* object) {
    for (;;) {
        const auto* parent = read<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return read<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
void clear_track(TargetTrack& track, float interval) {
    track.elapsed = 0;
    track.visible_hitgroups = 0;
    track.point = {};
    track.velocity_valid = 0;
    track.interval = interval;
}
Vector reference_forward(const Vector& angles, const lagcomp::RotationMath& math) {
    constexpr float radians = 0.01745329238474369f;
    const auto yaw = angles[1] * radians;
    const auto sy = math.sine(yaw), cy = math.cosine(yaw);
    const auto pitch = angles[0] * radians;
    const auto sp = math.sine(pitch), cp = math.cosine(pitch);
    Vector result{cp * cy, cp * sy, -sp};
    const auto inverse =
        1.0f / (std::sqrt((result[0] * result[0] + result[1] * result[1]) + result[2] * result[2]) +
                1.1920928955078125e-7f);
    for (auto& component : result)
        component = inverse * component;
    return result;
}
AccuracyEnvironment accuracy_environment(const RuntimeBindings& bindings,
                                         std::span<const TargetTrack, 64> targets) {
    auto result = bindings.accuracy;
    result.time = bindings.times.world_time;
    result.interval = bindings.times.interval;
    result.interpolation_43476928 = bindings.times.interpolation;
    result.maximum_clients = bindings.times.maximum_clients;
    result.player_punch = bindings.offsets.player_punch_1bae1168;
    result.targets = targets;
    return result;
}
Vector aim_command(RuntimeState& state, const client::CommandFrame& frame, void* player,
                   client::Command& command, const RuntimeSettings& settings,
                   RuntimeBindings& bindings, const TargetTrackingBindings& tracking) {
    auto& profile = state.profile;
    Vector output{};
    if (!profile.enabled ||
        !hotkey_active(settings.legit.aim_keys[std::size_t(profile.profile)].value.binding,
                       bindings.input) ||
        state.block_until > *bindings.times.command_time)
        return output;
    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    const auto release = [&] {
        release_target(state, *bindings.times.interval, bindings.random);
        return Vector{};
    };
    if (profile.limit_lock_time) {
        const auto ready = engine::invoke<bool>(weapon, 430);
        if (!(ready && (command.movement.buttons & 1u)) &&
            *bindings.times.command_time - state.manual_time > profile.maximum_lock_time)
            return release();
    }
    const auto reference = recoil_reference_angles(state, frame.previous_command.angles,
                                                   bindings.accuracy.recoil_scale);
    // The native owner computes this direction before the angular search even
    // though that search itself uses the two reference Euler components.
    (void)reference_forward(reference, bindings.accuracy.rotation);
    const auto target =
        select_aim_target(state,
                          {frame.origin_dc, reference, *bindings.times.maximum_clients,
                           *bindings.times.command_time, bindings.times.command_time},
                          {bindings.accuracy.targeting, tracking,
                           bindings.accuracy.rotation.arc_tangent2, bindings.random});
    if (!target.index)
        return output;
    if (!(profile.flags & 4u) && read<float>(player, bindings.offsets.blindness_1bc6d160) > 200.0f)
        return release();
    const auto& key = settings.legit.aim_keys[std::size_t(profile.profile)].value.binding;
    if ((key.packed & 0xfffffffcu) != 4u || key.mode == HotkeyMode::always) {
        const auto ready = engine::invoke<bool>(weapon, 430);
        if (!ready && (command.movement.buttons & 1u)) {
            const auto cycle = engine::invoke<float>(weapon, 433, 0);
            state.block_until = *bindings.times.command_time + cycle;
            return release();
        }
    }
    const auto frame_angles = frame.previous_command.angles;
    const auto ready =
        recoil_shot_ready(profile, weapon, command, bindings.offsets.ammunition_ca5ec698);
    const auto& recoil = profile.recoil_history[ready ? 1u : 0u];
    const auto pitch = normalize_movement_angle(target.angles[0] - (recoil[0] + frame_angles[0]));
    const auto yaw = normalize_movement_angle(target.angles[1] - (recoil[1] + frame_angles[1]));
    if (!(command.movement.buttons & 1u)) {
        const auto motion = std::sqrt(state.filtered_motion[0] * state.filtered_motion[0] +
                                      state.filtered_motion[1] * state.filtered_motion[1]);
        if (!(motion < 0.01f)) {
            const auto negative_yaw = -yaw;
            const auto inverse = 1.0f / (std::sqrt(pitch * pitch + negative_yaw * negative_yaw) +
                                         1.1920928955078125e-7f);
            const auto cosine = ((inverse * pitch) * state.filtered_motion[0] -
                                 (inverse * negative_yaw) * state.filtered_motion[1]) /
                                motion;
            if (cosine < -0.5f) {
                if (profile.secondary_flags & 2u) {
                    if (profile.secondary_flags & 4u)
                        state.block_until = *bindings.times.command_time + 0.1f;
                    return release();
                }
                if (target.field_of_view > std::sqrt(motion) * 10.0f)
                    return release();
            }
        }
    }
    output = calculate_aim_step(profile,
                                {pitch, yaw, target.field_of_view, target.distance,
                                 state.time_scale, state.curve, command.number},
                                bindings.random);
    if (recoil_shot_ready(profile, weapon, command, bindings.offsets.ammunition_ca5ec698)) {
        output[0] -= profile.recoil_history[2][0];
        output[1] -= profile.recoil_history[2][1];
        for (unsigned axis = 0; axis != 3; ++axis)
            profile.recoil_history[1][axis] += profile.recoil_history[2][axis];
    } else
        profile.recoil_history[1] = profile.recoil_history[0];
    if (profile.flags & 1u)
        bindings.scheduler_flags |= 8u;
    return output;
}
}
void capture_legit_view(RuntimeState& state, void* engine_client) {
    Vector angles;
    engine::invoke<void>(engine_client, 18, angles.data());
    state.previous_view_valid = state.current_view_valid;
    state.previous_view = state.current_view;
    state.current_view = angles;
    if (!state.current_view_valid)
        state.current_view_valid = 1;
}
void process_legit_command(RuntimeState& state, client::CommandFrame& frame, void* player,
                           client::Command& command, const RuntimeSettings& settings,
                           RuntimeBindings& bindings) {
    const auto& offsets = bindings.offsets;
    const auto shots = read<std::int32_t>(player, offsets.player_shots_8cfc633a);
    state.new_shot = state.profile.shots < shots;
    state.profile.shots = shots;
    if (settings.rage_enabled.value && hotkey_active(settings.rage_key, bindings.input))
        return;
    state.pending_step = {};
    state.pending_fraction = 0;
    state.time_scale = *bindings.times.frame_delta * 64.0f;
    if (!frame.weapon_address)
        return;
    const auto requested = std::int32_t(std::bit_cast<std::int8_t>(frame.weapon_kind_124));
    if (!select_weapon_profile(settings.legit, state.profile, requested, command.movement.buttons))
        return;
    const FirstShotFrame first_shot{read<std::uint32_t>(&frame, 0x44), frame.origin_dc,
                                    read<Vector>(&frame, 0x174), *bindings.times.maximum_clients};
    apply_first_shot_delay(settings.legit, state.profile, first_shot, bindings.accuracy.targeting,
                           bindings.input, command.movement.buttons);
    if ((read<std::uint32_t>(player, offsets.player_effects_38ca5228) & 8u) ||
        read<std::uint8_t>(player, offsets.player_byte_043fa575) !=
            read<std::uint8_t>(player, offsets.player_byte_334b460c) ||
        difference(command.number, state.last_command) > 1)
        reset_runtime_live(state, bindings.times.interval, bindings.random);
    update_manual_motion(state, command, frame.previous_command.angles,
                         *bindings.times.command_time, bindings.accuracy.rotation);
    const auto forward =
        reference_forward(recoil_reference_angles(state, frame.previous_command.angles,
                                                  bindings.accuracy.recoil_scale),
                          bindings.accuracy.rotation);
    const AcquisitionTraceContext visibility_filter{bindings.accuracy.trace_filter_vtable,
                                                    bindings.accuracy.trace_entities};
    auto& players = bindings.accuracy.selection.players;
    const auto maximum_clients = *bindings.times.maximum_clients;
    for (std::int32_t index = 1; index <= maximum_clients; ++index) {
        auto* target = engine::invoke<void*>(bindings.accuracy.targeting.entity_list, 3, index);
        auto& track = state.targets[std::uint32_t(index) & 63u];
        if (!rage::target_is_eligible(bindings.accuracy.targeting, target)) {
            clear_track(track, *bindings.times.interval);
            continue;
        }
        track.interval = *bindings.times.interval;
        auto& cached = *players.get(std::uint32_t(index));
        const auto& bone_runtime = bindings.accuracy.selection.attachments.bones;
        const LiveHistoricalBoneBindings bones{bindings.historical_offsets,
                                               cached.native,
                                               bone_runtime,
                                               bindings.times.interval,
                                               bindings.times.world_time,
                                               bindings.times.setup_bones_time,
                                               bindings.times.clock_tick};
        if (!prepare_target_bones_live(state.profile, cached.history, target, bindings.target_bones,
                                       bindings.bone_scratch, track.interval, bones)) {
            clear_track(track, *bindings.times.interval);
            continue;
        }
        const StudioOffsets studio{bone_runtime.fields.studio_wrapper_32e4daf1,
                                   bone_runtime.fields.hitbox_set_c2b6fb10};
        acquire_visible_target_live(track, state.profile, target, studio, bindings.target_bones,
                                    frame.origin_dc, forward, *bindings.times.interval,
                                    bindings.accuracy.ballistics, visibility_filter,
                                    bindings.accuracy.trace);
    }
    if (frame.flags & 0x10u)
        return;
    auto decay = bindings.recoil_decay;
    decay.interval = *bindings.times.interval;
    RecoilStateInput recoil{
        read<Vector>(player, offsets.player_punch_1bae1168),
        read<Vector>(&frame, 0x8C),
        read<Vector>(player, offsets.player_punch_velocity_48442721),
        read<float>(player, offsets.player_next_attack_de530a8b),
        read<float>(reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address)),
                    offsets.weapon_next_attack_f71ce331),
        *bindings.times.world_time,
        command.movement.buttons};
    update_recoil_state(state, recoil, bindings.accuracy.recoil_scale, decay, bindings.random);
    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    if (!weapon ||
        !lagcomp::history_weapon_is_firearm(
            read<std::uint16_t>(weapon, bindings.accuracy.targeting.offsets.weapon_identifier)) ||
        read<std::int32_t>(weapon, offsets.ammunition_ca5ec698) < 1 ||
        read<std::uint8_t>(weapon, offsets.reloading_b8969256))
        return;
    if (!(state.profile.secondary_flags & 1u) && !state.profile.enabled)
        clear_recoil_history(state.profile);
    std::array<lagcomp::RecordRing, 64> ring_views;
    const TargetTrackingBindings tracking{ring_views,
                                          state.targets,
                                          bindings.historical_offsets.common.velocity,
                                          *bindings.times.history_command_tick,
                                          *bindings.times.interval,
                                          &players,
                                          bindings.times.history_command_tick,
                                          bindings.times.interval};
    auto output = aim_command(state, frame, player, command, settings, bindings, tracking);
    auto& profile = state.profile;
    if ((profile.secondary_flags & 1u) && !state.target && output[0] == 0.0f && output[1] == 0.0f) {
        if (recoil_shot_ready(profile, weapon, command, offsets.ammunition_ca5ec698)) {
            output[0] -= profile.recoil_history[2][0];
            output[1] -= profile.recoil_history[2][1];
            for (unsigned axis = 0; axis != 3; ++axis)
                profile.recoil_history[1][axis] += profile.recoil_history[2][axis];
        } else
            clear_recoil_history(profile);
    }
    if (output[0] != 0.0f || output[1] != 0.0f) {
        output = {normalize_movement_angle(output[0]), normalize_movement_angle(output[1]), 0.0f};
        if (std::sqrt(output[0] * output[0] + output[1] * output[1]) < 2.0f) {
            state.pending_step = output;
            state.pending_fraction = 1.0f;
            apply_pending_aim(state, command, read<float>(&frame, 0x1B4), *bindings.times.interval);
            client::set_command_view_angles(bindings.view_angles, command.angles);
            const auto saved = read<Vector>(&frame, 0x118);
            if (frame.previous_command.angles == saved)
                write(&frame, 0x118, command.angles);
        }
    }
    std::array<float, 64> target_times;
    for (unsigned index = 0; index != 64; ++index)
        target_times[index] = state.targets[index].elapsed;
    const TriggerFrame trigger_frame{frame.flags, std::uint32_t(frame.weapon_id_d8),
                                     *bindings.times.world_time,
                                     read<float>(player, offsets.blindness_1bc6d160)};
    TriggerCommand trigger_command{command.movement.buttons, command.tick};
    if (begin_trigger(settings.legit, profile, trigger_frame, bindings.input, trigger_command)) {
        command.movement.buttons = trigger_command.buttons;
        bool hit = false;
        auto selected_tick = command.tick;
        if (profile.accuracy_boost) {
            auto accuracy = accuracy_environment(bindings, state.targets);
            selected_tick = accuracy_tick(profile, frame, player, command, &hit, accuracy);
        } else {
            auto angles = command.angles;
            add_player_recoil(angles, read<Vector>(player, offsets.player_punch_1bae1168),
                              bindings.accuracy.recoil_scale,
                              bindings.accuracy.normalize_angles_434693ec);
            auto query = rage::prepare_target_bullet(
                weapon, bindings.accuracy.targeting.offsets.weapon_identifier,
                bool(profile.flags & 0x80u));
            const RageTrigonometry trigonometry{bindings.accuracy.rotation.sine,
                                                bindings.accuracy.rotation.cosine};
            const auto basis = spread_basis(angles, trigonometry);
            const rage::SpreadParameters parameters{
                std::uint32_t(frame.weapon_id_d8),
                std::bit_cast<std::int32_t>(frame.field_b0),
                read<float>(&frame, 0xD0),
                std::bit_cast<float>(frame.field_c4),
                std::bit_cast<float>(frame.field_100),
                1,
                convar_enabled(bindings.accuracy.shotgun_patterns_convar),
                frame.weapon_kind_124};
            const TriggerTraceInput trace_input{
                std::uint32_t(reinterpret_cast<std::uintptr_t>(player)),
                frame.origin_dc,
                basis,
                parameters,
                read<std::uint32_t>(&frame, 0x7C),
                target_times,
                state.targets.data()};
            BulletTraceFilter native_filter(bindings.accuracy.trace_filter_vtable);
            const auto filter = native_filter.binding();
            FilterBallistics engine(bindings.accuracy.ballistics);
            hit = trace_trigger(engine, profile, trace_input, filter, query,
                                bindings.accuracy.spread_tables, bindings.accuracy.random,
                                bindings.accuracy.shotgun_pattern, trigonometry,
                                bindings.accuracy.trace);
        }
        // The native continuation reads the command after the complete trace
        // or historical transaction, while selected_tick retains its saved
        // pre-trace value on the ordinary128-seed path.
        trigger_command = {command.movement.buttons, command.tick};
        finish_trigger_live(profile, &frame.flags, bindings.times.world_time, hit, selected_tick,
                            trigger_command, bindings.random);
    }
    command.movement.buttons = trigger_command.buttons;
    command.tick = trigger_command.tick;
    if (profile.accuracy_boost &&
        ((frame.flags & 0x100000u) || ((frame.flags & 0x400u) && (command.movement.buttons & 1u)) ||
         ((frame.flags & 2u) && (command.movement.buttons & 0x800u) &&
          (frame.weapon_id_d8 == 64 || !frame.weapon_kind_124)))) {
        auto accuracy = accuracy_environment(bindings, state.targets);
        const auto tick = accuracy_tick(profile, frame, player, command, nullptr, accuracy);
        if (tick != -1)
            command.tick = tick;
    }
}
} // namespace skeet::legit
