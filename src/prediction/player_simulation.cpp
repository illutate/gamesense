#include "skeet/prediction/player_simulation.hpp"
#include "skeet/prediction/observer_registry.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include "skeet/engine/unload.hpp"
#include <bit>
#include <cstring>

namespace skeet::prediction {
namespace {
template <class T>
[[gnu::always_inline]] inline T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
[[gnu::always_inline]] inline float multiply(float first, float second) noexcept {
    __asm__ __volatile__("mulss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
template <bool Installed>
void original_simulation(void* player, const PlayerSimulationBindings& bindings) {
    void** table;
    if constexpr (Installed) {
        const auto installed = *static_cast<void***>(player);
        const auto* metadata = static_cast<const engine::VtableHookMetadata*>(installed[-2]);
        table = metadata->original_table;
    } else {
        table = bindings.original_player_table;
    }
    reinterpret_cast<engine::MemberFunction<void>>(table[155])(player);
}
template <bool Installed>
void simulate_player_command_impl(void* player, const PlayerSimulationBindings& bindings) {
    const auto& o = bindings.offsets;
    auto& context = *reinterpret_cast<PlayerCommandContext*>(static_cast<std::byte*>(player) +
                                                             o.command_context);
    if (!read<std::uint8_t>(player, o.simulation_enabled) || !context.needs_processing ||
        read<std::uint8_t>(player, o.rejection_byte) ||
        *bindings.simulation_tick == read<std::int32_t>(player, o.simulation_tick) ||
        engine::invoke<bool>(bindings.engine_client, 82) ||
        engine::invoke<bool>(bindings.engine_client, 93)) {
        original_simulation<Installed>(player, bindings);
        const auto scale =
            std::bit_cast<float>(lagcomp::read_prediction_convar(bindings.recoil_scale_convar));
        write(player, o.scaled_recoil_pitch, multiply(read<float>(player, o.recoil_angles), scale));
        return;
    }
    if (context.command.tick == 0x7F7FFFFF) {
        bindings.observers.rejected(player, context);
        write(player, o.simulation_tick, *bindings.simulation_tick);
        context.needs_processing = 0;
        return;
    }
    bindings.observers.capture_before(player, context.command);
    if (!bindings.observers.owners().empty()) {
        const auto tick = bindings.observers.adjust_tick(context.sequence_number,
                                                         read<std::int32_t>(player, o.tick_base));
        write(player, o.tick_base, tick);
    }
    std::uint8_t* attack_guard = nullptr;
    std::uint8_t saved_guard;
    if (context.command.movement.buttons & 0x801u) {
        attack_guard =
            reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(player) + o.attack_guard);
        saved_guard = *attack_guard;
        *attack_guard = 0;
    }
    original_simulation<Installed>(player, bindings);
    if (attack_guard)
        *attack_guard = saved_guard;
    bindings.observers.finish_prediction(player, context.command);
}
}
void simulate_player_command(void* player, const PlayerSimulationBindings& bindings) {
    simulate_player_command_impl<false>(player, bindings);
}
void simulate_hooked_player_command(void* player, const PlayerSimulationBindings& bindings) {
    simulate_player_command_impl<true>(player, bindings);
}
} // namespace skeet::prediction
