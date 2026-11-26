#include "skeet/misc/viewmodel_lag.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include <bit>
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void assign(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t signed_word(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}
bool available(const ViewmodelLagBindings& b) {
    return !engine::invoke<bool>(b.engine, 82) && !engine::invoke<bool>(b.engine, 93);
}
void* viewmodel(const ViewmodelLagBindings& b, void* player) {
    const auto handles = b.offsets.handles;
    for (unsigned index = 0; index != 2; ++index) {
        const auto handle = field<std::uint32_t>(player, handles + index * 4);
        if (handle == 0xFFFFFFFF)
            continue;
        auto* entity = engine::invoke<void*>(b.entity_list, 4, handle);
        if (entity && !(field<std::uint32_t>(entity, b.offsets.flags) & 0x20))
            return entity;
    }
    return nullptr;
}
bool interpolation_enabled(const void* variable) { // complete43407DE2
    for (;;) {
        const auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    const auto encoded = field<std::uint32_t>(variable, 0x2C);
    return std::bit_cast<float>(encoded ^ static_cast<std::uint32_t>(
                                              reinterpret_cast<std::uintptr_t>(variable))) != 0;
}
void** original_table(void* receiver) {
    auto** installed = *static_cast<void***>(receiver);
    return static_cast<const engine::VtableHookMetadata*>(installed[-2])->original_table;
}
}
void initialize_viewmodel_lag(ViewmodelLagState& state) noexcept {
    state.previous_animation_time = 0;
    for (auto& record : state.snapshots)
        record.weapon_handle = 0xFFFFFFFF;
}
void reset_viewmodel_lag(ViewmodelLagState& state) noexcept {
    state.tick_base = state.last_command = state.prediction_generation = 0;
    for (auto& record : state.ticks)
        record.command = -1;
    for (auto& record : state.snapshots) {
        record.command = 0xFFFFFFFF;
        record.weapon_handle = 0xFFFFFFFF;
    }
}
void record_viewmodel_command(ViewmodelLagState& state, const client::Command& command) noexcept {
    state.last_command = command.number;
    state.ticks[command.number % 150] = {command.number, state.tick_base};
}
void record_viewmodel_command_range(ViewmodelLagState& state, const ViewmodelLagBindings& b,
                                    std::int32_t final_command, std::int32_t count) {
    if (!b.enabled || !available(b))
        return;
    state.last_command = final_command;
    auto command = signed_word(std::uint32_t(final_command) - std::uint32_t(count));
    while (count > 0) {
        command = signed_word(std::uint32_t(command) + 1);
        state.ticks[command % 150] = {command, state.tick_base};
        state.tick_base = signed_word(std::uint32_t(state.tick_base) + 1);
        --count;
    }
}
void advance_viewmodel_prediction(ViewmodelLagState& state, const ViewmodelLagBindings& b) {
    if (b.enabled && available(b))
        state.prediction_generation = signed_word(std::uint32_t(state.prediction_generation) + 1);
}
void consume_viewmodel_prediction(ViewmodelLagState& state) noexcept {
    if (state.prediction_generation > 0)
        --state.prediction_generation;
    else
        state.tick_base = signed_word(std::uint32_t(state.tick_base) + 1);
}
void prepare_viewmodel_prediction(ViewmodelLagState& state, const ViewmodelLagBindings& b,
                                  void* player, const client::Command& command) {
    state.command_number = command.number;
    state.previous_query = state.previous_generation = -1;
    if (!b.enabled || !available(b))
        return;
    if (auto* entity = viewmodel(b, player))
        state.previous_animation_time = field<float>(entity, b.offsets.animation_base - 8);
}
void capture_viewmodel_prediction(ViewmodelLagState& state, const ViewmodelLagBindings& b,
                                  void* player, const client::Command& command) {
    state.command_number = state.previous_query = state.previous_generation = -1;
    if (!b.enabled || !available(b))
        return;
    auto* entity = viewmodel(b, player);
    if (!entity)
        return;
    const auto animation_time = field<float>(entity, b.offsets.animation_base - 8);
    if (animation_time != state.previous_animation_time && animation_time == *b.current_time) {
        const auto& mapped = state.ticks[command.number % 150];
        if (mapped.command == command.number)
            assign(entity, b.offsets.animation_base - 8,
                   static_cast<float>(mapped.tick_base) * *b.interval);
    }
    auto& record = state.snapshots[std::uint32_t(command.number) % 150];
    record.command = std::uint32_t(command.number);
    record.random_seed = std::uint32_t(command.random_seed);
    record.weapon_handle = field<std::uint32_t>(entity, b.offsets.weapon_handle);
    record.sequence = field<std::int32_t>(entity, b.offsets.sequence);
    record.animation_token_08 = field<std::int32_t>(entity, b.offsets.animation_token_08);
    record.animation_token_0c = field<std::int32_t>(entity, b.offsets.animation_token_0c);
    record.field_18 = field<std::uint32_t>(entity, b.offsets.field_18);
    record.player_byte_1c = field<std::uint8_t>(player, b.offsets.player_byte);
    record.selected_model = field<std::int16_t>(entity, b.offsets.selected_model);
}
void copy_previous_viewmodel_snapshot(ViewmodelLagState& state, const ViewmodelLagBindings& b,
                                      std::int32_t command_number) {
    if (!b.enabled || !available(b))
        return;
    // Slot3 receives CCommandContext's base. Its +8 is the embedded
    // Command.number, because the Command itself begins at context+4.
    const auto number = std::uint32_t(command_number);
    auto& destination = state.snapshots[number % 150];
    destination = state.snapshots[(number - 1) % 150];
    destination.command = number;
}
void repair_viewmodel_lag(ViewmodelLagState& state, const ViewmodelLagBindings& b, void* player) {
    if (!b.enabled || !available(b) || !available(b) ||
        !interpolation_enabled(b.interpolation_cvar))
        return;
    auto* entity = viewmodel(b, player);
    if (!entity)
        return;
    const auto animation_time = field<float>(entity, b.offsets.animation_base - 8);
    if (animation_time != field<float>(entity, b.offsets.animation_base - 4) &&
        animation_time == *b.current_time) {
        const auto value = field<float>(entity, b.offsets.animation_value);
        if (value != field<float>(entity, b.offsets.sequence + 0x30) && value == 0) {
            const auto acknowledged = engine::invoke<std::uint32_t>(b.engine, 205);
            const auto& record = state.snapshots[acknowledged % 150];
            if (record.command == acknowledged &&
                record.weapon_handle == field<std::uint32_t>(entity, b.offsets.weapon_handle) &&
                record.sequence == field<std::int32_t>(entity, b.offsets.sequence) &&
                record.animation_token_08 ==
                    field<std::int32_t>(entity, b.offsets.animation_token_08) &&
                record.animation_token_0c ==
                    field<std::int32_t>(entity, b.offsets.animation_token_0c) &&
                record.selected_model == field<std::int16_t>(entity, b.offsets.selected_model)) {
                assign(entity, b.offsets.animation_base - 8,
                       field<std::uint32_t>(entity, b.offsets.animation_base - 4));
                assign(entity, b.offsets.animation_value,
                       field<std::uint32_t>(entity, b.offsets.sequence + 0x30));
            }
        }
    }
    if (field<float>(entity, b.offsets.animation_base - 8) != *b.current_time)
        return;
    const auto acknowledged = engine::invoke<std::int32_t>(b.engine, 205);
    const auto& mapped = state.ticks[acknowledged % 150];
    if (mapped.command == acknowledged)
        assign(entity, b.offsets.animation_base - 8,
               static_cast<float>(mapped.tick_base) * *b.interval);
}
bool viewmodel_method_113(ViewmodelLagState* state, const ViewmodelLagHookBindings& b,
                          void* receiver, float argument) { // complete43324F45
    void* saved_player = nullptr;
    std::int32_t saved_tick = 0;
    if (state && b.runtime.enabled && available(b.runtime)) {
        const auto* commands = field<const client::Command*>(b.input, 0xF0);
        const auto number = std::uint32_t(state->last_command);
        const auto& command = commands[number % 150];
        if (std::uint32_t(command.number) == number && std::uint32_t(command.tick) == 0x7F7FFFFF)
            return true;
        if (b.local_player) {
            saved_player = b.local_player;
            saved_tick = field<std::int32_t>(saved_player, b.player_tick_base);
            assign(saved_player, b.player_tick_base, state->tick_base);
        }
    }
    const auto original =
        reinterpret_cast<engine::MemberFunction<bool, float>>(original_table(receiver)[113]);
    const auto result = original(receiver, argument);
    if (saved_player)
        assign(saved_player, b.player_tick_base, saved_tick);
    return result;
}
std::uint32_t viewmodel_method_206(ViewmodelLagState* state, const ViewmodelLagHookBindings& b,
                                   void* receiver, std::array<std::uint32_t, 4> arguments,
                                   float argument) { // complete43359D27
    float* saved_target = nullptr;
    float saved_time = 0;
    if (state && b.runtime.enabled && available(b.runtime)) {
        saved_target = b.prediction_time;
        saved_time = *saved_target;
        const auto prior_tick = signed_word(std::uint32_t(state->tick_base) - 1);
        const auto tick_time = static_cast<float>(prior_tick) * *b.runtime.interval;
        const auto fraction_time = *b.interpolation_fraction * *b.runtime.interval;
        *saved_target = tick_time + fraction_time;
    }
    const auto original =
        reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uint32_t, std::uint32_t,
                                                std::uint32_t, std::uint32_t, float>>(
            original_table(receiver)[206]);
    const auto result =
        original(receiver, arguments[0], arguments[1], arguments[2], arguments[3], argument);
    if (saved_target)
        *saved_target = saved_time;
    return result;
}
void viewmodel_method_247(ViewmodelLagState* state, const ViewmodelLagHookBindings& b,
                          void* receiver, std::int32_t argument) { // complete4336BA27
    if (state && b.runtime.enabled && available(b.runtime) && state->command_number != -1) {
        if (state->command_number == state->previous_generation &&
            state->previous_query == argument)
            return;
        state->previous_generation = state->command_number;
        state->previous_query = argument;
    }
    const auto original =
        reinterpret_cast<engine::MemberFunction<void, std::int32_t>>(original_table(receiver)[247]);
    original(receiver, argument);
}
} //namespace skeet::misc
