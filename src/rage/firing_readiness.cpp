#include "skeet/rage/firing.hpp"
#include "skeet/engine/interface_call.hpp"
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
bool convar_nonzero(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1c);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
std::int32_t subtract_wrapped(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
}
std::int32_t firing_mode(bool mode_enabled, bool send_mode) noexcept {
    return mode_enabled ? (send_mode ? 2 : 1) : 0;
}
FiringReadiness evaluate_firing_readiness(FiringState& state, const FiringTargetData& target,
                                          FiringBranchState& branch, FiringSharedState& shared,
                                          client::Command& command,
                                          const FiringReadinessBindings& bindings) {
    const auto& candidate = target.candidate;
    const auto near_tick = [&](std::int32_t tick) {
        if (target.field_b4 <= 0)
            return true;
        const auto time = float(tick) * bindings.tick_interval;
        const auto cap = float(bindings.latency_milliseconds) * 0.001f;
        const auto corrected = minimum_number(state.field_198 + bindings.current_time, cap);
        const auto record_time = float(target.field_b4) * bindings.tick_interval;
        return std::fabs(corrected - (time - record_time)) <= 0.2f;
    };
    const auto remember = [&] {
        shared.selected_marker = state.marker;
        shared.selected_point = candidate.primary.position;
    };
    bool newly_ready = false;
    if (!branch.field_22) {
        if (!branch.timing_ready && !branch.field_83) {
            if (!branch.field_33 || !near_tick(state.current_tick)) {
                remember();
                return FiringReadiness::retained_target;
            }
            state.flags |= 0x400U;
            state.previous_tick = state.current_tick;
            branch.alternate_path = true;
            branch.timing_ready = true;
            newly_ready = true;
            bindings.prediction_time = float(state.current_tick) * bindings.tick_interval;
        } else if (candidate.primary.flags & 0x30U) {
            auto& player = *bindings.players.get(candidate.entity_index);
            const auto receive =
                player.history.count
                    ? player.history.records[player.history.first & 31U]->header.receive_tick
                    : field<std::int32_t>(&player.native, 0x168);
            const auto time = engine::invoke<float>(bindings.engine_client, 14);
            const auto age =
                truncate(float(static_cast<long double>(time) /
                                   static_cast<long double>(bindings.receive_interval) +
                               static_cast<long double>(bindings.correction_time)));
            if (subtract_wrapped(age, receive) > 2) {
                remember();
                return FiringReadiness::retained_target;
            }
        }
    }
    // The first successful timing transition jumps directly to43354FB5.
    if (!newly_ready && state.weapon_id != 64 && state.field_1d4 != 0 &&
        convar_nonzero(bindings.timing_convar) && state.previous_tick < state.current_tick &&
        target.field_b4 > 0 && !near_tick(state.previous_tick) && near_tick(state.current_tick)) {
        state.previous_tick = state.current_tick;
        branch.alternate_path = true;
        bindings.prediction_time = float(state.current_tick) * bindings.tick_interval;
    }
    branch.field_5b = false;
    branch.field_33 = false;
    if (!(state.flags & 0x40000000U) && !branch.alternate_path) {
        const auto mode = firing_mode(bindings.mode_enabled, bindings.send_mode);
        branch.field_5b =
            !branch.field_22 && bindings.enabled && mode == 1 && state.field_15c >= state.field_fc;
        if (branch.field_83)
            branch.field_33 = true;
        else if (candidate.primary.damage < candidate.health && !branch.field_22 &&
                 bindings.enabled && bindings.delay_option != 0 && mode == 2 &&
                 state.field_15c == 0 && !(bindings.scheduler_flags & 0x23U) &&
                 !(state.flags & 0x40U) && !(state.flags & 0x102000U) && state.field_1d4 != 2 &&
                 state.field_fc > 0) {
            const bool revolver_ok = state.weapon_id != 64 || !(state.flags & 0x8000U) ||
                                     (bindings.revolver_gate > bindings.prediction_time &&
                                      (command.movement.buttons & 0x800U));
            const bool marker_ok = bindings.committed_marker <= bindings.command_marker ||
                                   bindings.committed_marker >= state.marker;
            branch.field_33 =
                revolver_ok && marker_ok &&
                std::uint32_t(shared.last_delay_marker) + 1U != std::uint32_t(state.marker);
        }
    }
    auto& player = *bindings.players.get(candidate.entity_index);
    if (branch.field_5b || branch.field_33) {
        if (!branch.field_5b) {
            std::memcpy(reinterpret_cast<std::byte*>(&player.native) + 0x194, &target.field_13c, 4);
            *state.send_packet = 0;
            if (!branch.field_22)
                command.movement.buttons &= ~1U;
        }
        remember();
        shared.last_delay_marker = state.marker;
        return FiringReadiness::retained_target;
    }
    return branch.timing_ready || branch.field_22 ? FiringReadiness::angles
                                                  : FiringReadiness::stopped;
}
} // namespace skeet::rage
