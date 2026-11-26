#include "skeet/client/entity_command_owner.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/client/verified_command.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/legit/runtime.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace skeet::client {
namespace {
using Vector = std::array<float, 3>;
template <class T> T read(const void* object, std::ptrdiff_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t subtract(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - std::uint32_t(second));
}
prediction::CommandHistoryState* history_at(EntityCommandOwner& owner,
                                            std::int32_t number) noexcept {
    auto& entry = owner.state.history[number % 150];
    return entry.command_number == number ? &entry.state : nullptr;
}
const Command* command_at(const EntityCommandBindings& bindings, std::int32_t number) noexcept {
    const auto* commands = read<const Command*>(bindings.input, 0xF0);
    const auto& command = commands[std::uint32_t(number) % 150u];
    return command.number == number ? &command : nullptr;
}
float convar_float(const void* variable) noexcept {
    for (;;) {
        const auto* parent = read<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return std::bit_cast<float>(read<std::uint32_t>(variable, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(variable)));
}
std::int32_t processing_ticks(const EntityCommandBindings& bindings) noexcept {
    auto seconds = _mm_cvtepi32_ps(_mm_cvtsi32_si128(*bindings.process_milliseconds));
    seconds = _mm_div_ss(seconds, _mm_set_ss(1000.0f));
    seconds = _mm_min_ss(_mm_max_ss(seconds, _mm_setzero_ps()), _mm_set_ss(1.0f));
    seconds = _mm_div_ss(seconds, _mm_set_ss(*bindings.interval));
    return _mm_cvttss_si32(_mm_add_ss(seconds, _mm_set_ss(0.5f)));
}
std::int32_t receive_tick(const EntityCommandBindings& bindings) {
    const auto time = engine::invoke<long double>(bindings.engine_client, 14);
    const auto interval = static_cast<long double>(read<float>(bindings.engine_globals, 0x20));
    // FDIV/FADD retain the engine's ST0 return until one final FSTP float.
    volatile float rounded = static_cast<float>(time / interval + 0.5L);
    return _mm_cvttss_si32(_mm_set_ss(rounded));
}
void visit_cached_player(EntityCommandOwner& owner, void* player) {
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    static_cast<void>(owner.bindings.players->get(index));
}

void advance_short_acknowledgement(EntityCommandOwner& owner, void* player,
                                   prediction::CommandHistoryState& predicted, std::int32_t steps) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    if (predicted.weapon_identity != UINT32_MAX) {
        auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, predicted.weapon_identity);
        if (weapon) {
            auto tick = read<std::int32_t>(&predicted);
            const auto saved_accuracy = read<std::uint32_t>(weapon, offsets.weapon_field_d2563274);
            const auto saved_recoil = read<std::uint32_t>(weapon, offsets.weapon_field_6777c9ce);
            const auto saved_time = read<std::uint32_t>(bindings.simulation_time);
            for (std::int32_t iteration = 0; iteration < steps; ++iteration) {
                *bindings.simulation_time = float(tick) * *bindings.interval;
                auto decay = *bindings.recoil_decay;
                decay.interval = *bindings.interval;
                legit::decay_recoil(predicted.recoil_velocity, predicted.recoil_angles, decay);
                engine::invoke<void>(weapon, 484); //actual virtual+790
                write(&predicted, 0x44, read<std::uint32_t>(weapon, offsets.weapon_field_6777c9ce));
                write(&predicted, 0x64, read<std::uint32_t>(weapon, offsets.weapon_field_d2563274));
                tick = add(tick, 1);
            }
            write(bindings.simulation_time, 0, saved_time);
            write(weapon, offsets.weapon_field_6777c9ce, saved_recoil);
            write(weapon, offsets.weapon_field_d2563274, saved_accuracy);
        }
    }
    write(&predicted, 0, read<std::uint32_t>(player, offsets.tick_base));
}

void observe_near_vector(const Vector& actual, const Vector& predicted) noexcept {
    //4340426B/42B1/42F7 only inspect components. They do not restore vectors;
    // keep these comparisons distinct from repair_entity_prediction_fields.
    for (unsigned component = 0; component != 3; ++component)
        if (!(std::fabs(actual[component] - predicted[component]) <= 0.03125f))
            break;
}
void visit_acknowledged_state(EntityCommandOwner& owner, void* player,
                              const prediction::CommandHistoryState& predicted,
                              std::int32_t acknowledged) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    observe_near_vector(read<Vector>(player, offsets.vector_5ae32976), read<Vector>(&predicted, 4));
    observe_near_vector(read<Vector>(player, offsets.vector_1bae1168), predicted.recoil_angles);
    observe_near_vector(read<Vector>(player, offsets.vector_48442721), predicted.recoil_velocity);
    const auto handle = read<std::uint32_t>(player, offsets.active_weapon);
    if (predicted.weapon_identity == handle && handle != UINT32_MAX) {
        if (auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, handle))
            static_cast<void>(entity_command_view_model(
                owner, player,
                read<std::int32_t>(weapon, offsets.weapon_viewmodel_index_c6c78c82)));
    }
    if (command_at(bindings, acknowledged))
        visit_cached_player(owner, player);
}

void retime_acknowledged_history(EntityCommandOwner& owner, void* player,
                                 prediction::CommandHistoryState& predicted,
                                 const EntityDataUpdateWork& work) {
    const auto& bindings = owner.bindings;
    const auto acknowledged = engine::invoke<std::int32_t>(bindings.engine_client, 205);
    auto tick = read<std::int32_t>(player, bindings.offsets.tick_base);
    const auto delta = subtract(acknowledged, *bindings.old_acknowledged);
    if (read<std::int32_t>(bindings.engine_globals, 0x18) != 1 &&
        convar_float(bindings.interpolation_enabled) != 0.0f &&
        (std::uint32_t(delta) & 0xFFFFFFC0u) == 0u) {
        const auto processing = processing_ticks(bindings);
        const auto received = receive_tick(bindings);
        const auto center = add(received, processing);
        const auto captured = add(owner.state.data_update.tick_base, delta);
        if (captured <= add(center, processing) && captured >= received)
            return;
        const auto adjusted =
            add(subtract(read<std::int32_t>(bindings.engine_globals, 0x28), delta), center);
        if (adjusted == -1)
            return;
    }
    const auto received = receive_tick(bindings);
    auto expected = add(add(received, 1), processing_ticks(bindings));
    auto number = add(*bindings.old_acknowledged, 1);
    std::uint32_t inspected = 0;
    while (number <= acknowledged && inspected < 150u) {
        const auto* command = command_at(bindings, number);
        if (!command)
            break;
        if (command->tick == 0x7F7FFFFF)
            expected = subtract(expected, 1);
        ++inspected;
        number = add(number, 1);
    }
    if (expected != tick)
        return;
    const auto choked = *bindings.choked_commands;
    const auto outgoing = *bindings.last_outgoing;
    const auto last = add(choked, outgoing);
    for (number = acknowledged; number <= last; number = add(number, 1)) {
        auto* record = history_at(owner, number);
        if (!record)
            break;
        const auto* command = command_at(bindings, number);
        if (!command)
            break;
        write(record, 0, tick);
        if (command->tick != 0x7F7FFFFF)
            tick = add(tick, 1);
    }
    visit_acknowledged_state(owner, player, predicted, work.acknowledged_command);
}

bool copy_matching_history(EntityCommandOwner& owner, void* player,
                           prediction::CommandHistoryState& destination, std::int32_t number,
                           bool compare_angles) {
    auto* source = history_at(owner, number);
    if (!source)
        return false;
    const auto flags =
        entity_data_update_difference_flags(owner, player, *source, number, compare_angles);
    const auto allowed = compare_angles ? 0x201u : 0xA01u;
    if ((flags & ~allowed) != 0u)
        return false;
    std::memcpy(&destination, source, sizeof(destination)); //actual msvcrt memcpy774FD4A0
    write(&destination, 0, read<std::uint32_t>(player, owner.bindings.offsets.tick_base));
    invalidate_command_prediction(
        owner.bindings.prediction); //433D11F7, before the repeated inline invalidation
    return true;
}
}

void reconcile_entity_data_update_ack(EntityCommandOwner& owner, void* player,
                                      const EntityDataUpdateWork& work) {
    if (work.acknowledged_delta <= 0 && work.captured_tick_base == work.current_tick_base)
        return;
    auto* acknowledged = history_at(owner, work.acknowledged_command);
    if (!acknowledged)
        return;
    if (read<std::int32_t>(acknowledged) == work.current_tick_base)
        return;

    if (work.acknowledged_delta <= 0 && work.captured_tick_base != work.current_tick_base) {
        const auto steps = subtract(work.current_tick_base, work.captured_tick_base);
        if (std::uint32_t(subtract(steps, 1)) <= 14u)
            advance_short_acknowledgement(owner, player, *acknowledged, steps);
    } else {
        const auto flags = entity_data_update_difference_flags(owner, player, *acknowledged,
                                                               work.acknowledged_command);
        if ((flags & ~0x201u) == 0u) {
            retime_acknowledged_history(owner, player, *acknowledged, work);
        } else {
            const auto prior_number =
                subtract(engine::invoke<std::int32_t>(owner.bindings.engine_client, 205), 1);
            if (!copy_matching_history(owner, player, *acknowledged, prior_number, true) &&
                read<std::int32_t>(acknowledged) <
                    read<std::int32_t>(player, owner.bindings.offsets.tick_base)) {
                const auto next_number =
                    add(engine::invoke<std::int32_t>(owner.bindings.engine_client, 205), 1);
                static_cast<void>(
                    copy_matching_history(owner, player, *acknowledged, next_number, false));
            }
        }
    }
    //43404C7E..4CCC repeats the same three writes even after433D11F7.
    invalidate_command_prediction(owner.bindings.prediction);
}
} // namespace skeet::client
