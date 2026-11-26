#include "skeet/network/packet_scheduler.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T out;
    std::memcpy(&out, static_cast<const std::byte*>(object) + offset, sizeof out);
    return out;
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffU);
}
void force_send(std::uint32_t& flags) noexcept {
    flags = (flags & ~0x400U) | 0x200U;
}
std::int32_t increment(std::int32_t value) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) + 1U);
}
}
void* nearest_enemy(const rage::TargetBindings& bindings, std::int32_t maximum_clients) {
    void* selected = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (std::int32_t i = 1; i <= maximum_clients; i = increment(i)) {
        const auto candidate = engine::invoke<void*, std::int32_t>(bindings.entity_list, 3, i);
        if (!candidate || !engine::invoke<bool>(candidate, 158) ||
            candidate == bindings.local_player ||
            field<std::uint8_t>(candidate, bindings.offsets.rejection_bytes[0]) != 0 ||
            engine::invoke<std::int32_t>(candidate, 122) <= 0 ||
            engine::invoke<bool>(static_cast<std::byte*>(candidate) + 8, 9) ||
            field<std::uint8_t>(candidate, bindings.offsets.rejection_bytes[1]) != 0)
            continue;
        auto relationship = bindings;
        relationship.local_player = candidate;
        if (!rage::target_is_enemy(relationship, bindings.local_player))
            continue;
        const auto local = field<Vector>(bindings.local_player, bindings.offsets.origin);
        const auto other = field<Vector>(candidate, bindings.offsets.origin);
        const float dx = local.x - other.x, dy = local.y - other.y, dz = local.z - other.z;
        const float candidate_distance = (dy * dy + dx * dx) + dz * dz;
        if (candidate_distance < distance) {
            selected = candidate;
            distance = candidate_distance;
        }
    }
    return selected;
}
void apply_fake_lag_triggers(FakeLagState& state, const FakeLagTriggerFrame& frame,
                             const anti_aim::TargetState& target,
                             FakeLagTriggerEnvironment& environment, std::uint32_t& flags,
                             bool& collision_result) {
    const auto target_state = std::bit_cast<std::int32_t>(target.fields_18[0]);
    if (state.tracked_target != target.handle)
        state.flags = (state.flags & ~2U) | (target.handle != -1 && target_state != 0 ? 2U : 0U);
    LagTriggerState trigger{state.flags, state.target_trigger_time, state.tracked_target};
    const bool changed = trigger_target_change(trigger, frame.value_c0, target.handle, target_state,
                                               environment.current_time, flags);
    state.flags = trigger.flags;
    state.target_trigger_time = trigger.last_time;
    if (!changed && (state.flags & 4U) && frame.packet.choked == 0) {
        state.flags &= ~4U;
        state.target = frame.packet.limit;
    }
    if (!(frame.packet.flags & 8U) && frame.weapon_type == 0 && frame.value_c0 > 0.1F &&
        frame.value_c0 < 260.0F && (frame.packet.flags & 0x40000U)) {
        const auto enemy = nearest_enemy(environment.targeting, environment.maximum_clients);
        if (enemy) {
            const auto handle = field<std::int32_t>(enemy, environment.active_weapon);
            if (handle != -1) {
                const auto weapon =
                    engine::invoke<void*, std::int32_t>(environment.world.entity_list, 4, handle);
                if (weapon && engine::invoke<std::int32_t>(weapon, 455) == 0) {
                    const auto origin = field<Vector>(enemy, environment.world.origin);
                    const float dx = origin.x - frame.packet.origin.x,
                                dy = origin.y - frame.packet.origin.y;
                    const float distance = std::sqrt(dy * dy + dx * dx);
                    if (distance < 500.0F) {
                        const float position_scale = 1.0F / (distance + 1.1920928955078125e-7F);
                        const auto& velocity = frame.packet.velocity;
                        const float velocity_scale =
                            1.0F / (std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y) +
                                    1.1920928955078125e-7F);
                        const float dot =
                            (((position_scale * dy) * distance) * velocity_scale) * velocity.y +
                            (((dx * position_scale) * distance) * velocity_scale) * velocity.x;
                        if (dot > 0.0F)
                            state.target = state.selected;
                        else if (dot < 0.0F)
                            force_send(flags);
                    }
                }
            }
        }
    }
    if (!(flags & 0x600U) && ((environment.rule_bf567619 && increment(frame.packet.choked) > 2 &&
                               engine::invoke<bool>(environment.engine_client, 224)) ||
                              state.value_7c == environment.last_outgoing))
        force_send(flags);
    if (target_collision_trigger(state, frame.packet, frame.predicted_origin, target.handle,
                                 environment.world, collision_result)) {
        state.target = frame.packet.limit;
        state.last_trigger_time = environment.current_time;
    } else if (absolute(environment.current_time - state.last_trigger_time) < 0.75F)
        state.target = frame.packet.limit;
    if (frame.tick_base % 100 == 0 ||
        selected_weapon_trigger(environment.weapons, frame.weapon_type, frame.selected_weapon) ||
        secondary_attack_trigger(environment.weapons, frame.weapon, frame.buttons,
                                 environment.current_time) ||
        reload_trigger(environment.weapons, frame.weapon, frame.buttons) ||
        (frame.value_c0 > 2.0F &&
         (velocity_trigger(state, frame.packet) ||
          displacement_trigger(state, frame.packet, frame.value_c0, frame.buttons,
                               environment.interval, environment.gravity))))
        state.target = std::max(state.target, state.selected);
}
} // namespace skeet::network
