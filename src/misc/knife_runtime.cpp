#include "skeet/misc/knife_runtime.hpp"
#include "skeet/misc/knife_aim.hpp"
#include "skeet/misc/knife_melee.hpp"
#include "skeet/misc/knife_record.hpp"
#include "skeet/rage/acquisition.hpp"
#include "skeet/client/command_orchestrator.hpp"
#include "skeet/lagcomp/record_selection_live.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t subtract(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - std::uint32_t(second));
}
bool convar_nonzero(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
void add_attack_button(client::Command& command, std::uint32_t score) noexcept {
    command.movement.buttons |= (score & 2U) ? 1U : ((score & 1U) << 11);
}
std::int32_t interpolation_ticks(const client::CommandFrame& frame, float interval) noexcept {
    return lagcomp::time_to_tick(field<float>(&frame, 0x198), interval);
}
std::int32_t target_attack_tick(const client::CommandFrame& frame, const void* target,
                                const KnifeRuntimeBindings& bindings) noexcept {
    const auto& entity = bindings.records.preparation.entity;
    return add(lagcomp::time_to_tick(field<float>(target, entity.offsets.simulation_time),
                                     entity.interval),
               interpolation_ticks(frame, entity.interval));
}
std::int32_t knife_history_age(const lagcomp::RecordRing& ring,
                               const KnifeRuntimeBindings& bindings) noexcept { // complete4340B19C
    if (!ring.count)
        return 1;
    const auto& entity = bindings.records.preparation.entity;
    const auto limit = client::query_command_budget(bindings.budget);
    const auto current = lagcomp::corrected_command_tick(
        entity.command_tick, entity.previous_command, entity.current_command);
    const auto age = subtract(current, ring.at(0).header.command_tick);
    if (limit < 0)
        return limit;
    if (age < 0)
        return 0;
    return age > limit ? limit : age;
}

void process_current_knife(const KnifeConfig& config, client::CommandFrame& frame,
                           client::Command& command, void* player, void* target,
                           const KnifeRuntimeBindings& bindings) {
    const auto& entity = bindings.records.preparation.entity;
    const auto absolute = *engine::invoke<const RageVector*>(target, 10);
    const auto origin = field<RageVector>(target, bindings.targeting.offsets.origin);
    const bool moved =
        origin[0] != absolute[0] || origin[1] != absolute[1] || origin[2] != absolute[2];
    if (moved && entity.set_absolute_origin)
        entity.set_absolute_origin(target, origin);
    std::uint32_t swing = 0;
    bool finished = false;
    const auto modes = std::uint32_t(config.mode.value);
    const auto direction = field<RageVector>(&frame, 0x1A8);
    if (modes & 1U) {
        swing = score_knife_attack(frame, player, direction, false, bindings.melee);
        if (!swing)
            finished = true; // Native swing miss exits before the stab query.
    }
    if (!finished) {
        const auto stab =
            (modes & 2U) ? score_knife_attack(frame, player, direction, true, bindings.melee) : 0U;
        if (stab > swing) {
            command.movement.buttons |= 0x800U;
            if (frame.flags & 2U)
                command.tick = target_attack_tick(frame, target, bindings);
        } else if (swing) {
            command.movement.buttons |= 1U;
            if (frame.flags & 0x400U)
                command.tick = target_attack_tick(frame, target, bindings);
        }
    }
    if (moved && entity.set_absolute_origin)
        entity.set_absolute_origin(target, absolute);
}
}

void* nearest_knife_target(const RageVector& origin, const rage::TargetBindings& bindings,
                           std::int32_t maximum_clients) {
    void* selected = nullptr;
    float nearest = 250000.0f;
    for (std::int32_t index = 1; index <= maximum_clients; index = add(index, 1)) {
        auto* target = engine::invoke<void*>(bindings.entity_list, 3, index);
        if (!rage::target_is_eligible(bindings, target) ||
            field<std::uint8_t>(target, bindings.offsets.movement_type) == 8)
            continue;
        const auto position = field<RageVector>(target, bindings.offsets.origin);
        const float dy = origin[1] - position[1], dx = origin[0] - position[0],
                    dz = origin[2] - position[2];
        const float distance = (dy * dy + dx * dx) + dz * dz;
        if (distance < nearest) {
            nearest = distance;
            selected = target;
        }
    }
    return selected;
}

void process_knife_command(const KnifeConfig& config, client::CommandFrame& frame,
                           client::Command& command, void* player, std::uint8_t& send_packet,
                           std::uint32_t& scheduler_flags, const KnifeRuntimeBindings& bindings) {
    if (!knife_command_enabled(config, frame, command, bindings.rage_enabled))
        return;
    process_enabled_knife_command(config, frame, command, player, send_packet, scheduler_flags,
                                  bindings);
}
bool knife_command_enabled(const KnifeConfig& config, const client::CommandFrame& frame,
                           const client::Command& command,
                           const std::uint8_t* rage_enabled) noexcept {
    if (!config.enabled.value || !frame.weapon_address || frame.weapon_kind_124 ||
        (command.movement.buttons & 0x801U))
        return false;
    return !*rage_enabled || (frame.flags & 0x402U);
}
void process_enabled_knife_command(const KnifeConfig& config, client::CommandFrame& frame,
                                   client::Command& command, void* player,
                                   std::uint8_t& send_packet, std::uint32_t& scheduler_flags,
                                   const KnifeRuntimeBindings& bindings) {
    auto* target =
        nearest_knife_target(frame.origin_10c, bindings.targeting, *bindings.maximum_clients);
    if (!target)
        return;
    if (!*bindings.rage_enabled) {
        process_current_knife(config, frame, command, player, target, bindings);
        return;
    }
    if (frame.choked_15c >= frame.choke_limit_fc && frame.choked_15c != 0)
        return;

    auto* networkable = static_cast<std::byte*>(target) + 8;
    const auto index = engine::invoke<std::uint32_t>(networkable, 10);
    auto& cached = *bindings.records.players.get(index);
    const auto health = engine::invoke<std::int32_t>(target, 122);
    const bool low_health = health <= (field<std::int32_t>(target, bindings.armor) > 0 ? 55 : 65);
    const auto second_index = engine::invoke<std::uint32_t>(networkable, 10);
    const auto& entity = bindings.records.preparation.entity;
    bool history_usable = false;
    if (convar_nonzero(bindings.candidates.lag_compensation)) {
        const auto& current = *bindings.records.players.get(second_index);
        history_usable = rage::target_history_is_usable(current.native, current.history, frame,
                                                        second_index, entity.interval);
    }
    const auto modes = std::uint32_t(config.mode.value);
    RageVector chosen;
    std::uint32_t best = 0;
    if (history_usable) {
        auto ticks =
            rage::collect_target_record_ticks(frame, target, cached, 1, bindings.candidates);
        if (ticks.empty() && cached.history.count) {
            const auto& newest = cached.history.at(0).header;
            if (!(frame.flags & 0x40U) && knife_history_age(cached.history, bindings) > 3) {
                const auto& velocity = newest.velocity;
                const float speed_squared =
                    (velocity[0] * velocity[0] + velocity[1] * velocity[1]) +
                    velocity[2] * velocity[2];
                if (speed_squared > 10.0f)
                    return;
            }
            ticks.push_back(newest.simulation_tick);
        }
        if (ticks.empty()) {
            evaluate_knife_direction(modes, frame, player, target, low_health, chosen, best,
                                     bindings.melee);
            if (!best ||
                !apply_knife_aim(frame, player, command, chosen, send_packet, bindings.aim))
                return;
            add_attack_button(command, best);
            command.tick = target_attack_tick(frame, target, bindings);
            return;
        }

        std::array<Matrix3x4, 256> saved_bones;
        lagcomp::EntityRecordScope scope(entity, target, saved_bones);
        std::int32_t chosen_tick = command.tick;
        for (const auto tick : ticks) {
            if (!lagcomp::select_and_apply_history_record(scope, bindings.records, tick, 0,
                                                          bindings.bone_runtime))
                break;
            const auto previous = best;
            evaluate_knife_direction(modes, frame, player, target, low_health, chosen, best,
                                     bindings.melee);
            if (best > previous) {
                chosen_tick = tick;
                if ((best & 0x12U) == 0x12U)
                    break;
            }
            if (scope.flags & 0x20U)
                break;
        }
        if (best && apply_knife_aim(frame, player, command, chosen, send_packet, bindings.aim)) {
            add_attack_button(command, best);
            command.tick = add(chosen_tick, interpolation_ticks(frame, entity.interval));
            send_packet = 0;
            scheduler_flags = (scheduler_flags & ~0x400U) | 0x200U;
        }
        return;
    }

    std::array<Matrix3x4, 256> saved_bones;
    lagcomp::EntityRecordScope scope(entity, target, saved_bones);
    apply_extrapolated_knife_record(scope, frame, bindings.extrapolation,
                                    bindings.records.current_bone_counter, bindings.bone_runtime,
                                    bindings.live_movement, bindings.budget);
    evaluate_knife_direction(modes, frame, player, target, low_health, chosen, best,
                             bindings.melee);
    if (best && apply_knife_aim(frame, player, command, chosen, send_packet, bindings.aim)) {
        add_attack_button(command, best);
        frame.flags |= 0x2000000U;
    }
}
} // namespace skeet::misc
