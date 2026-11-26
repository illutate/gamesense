#include "skeet/prediction/command.hpp"
#include "skeet/engine/interface_call.hpp"

namespace skeet::prediction {
void update_command_seed(client::Command& command, SeedGenerator generator) {
    command.random_seed = static_cast<std::int32_t>(generator(command.number) & 0x7FFFFFFFu);
}
void set_prediction_flags(PredictionFlagNode* first, std::uint8_t value) noexcept {
    for (auto* node = first; node; node = node->next)
        node->enabled = value;
}

TimeScope::TimeScope(TimeTargets input, bool paused, float interval,
                     std::int32_t tick_base) noexcept
    : targets(input) {
    frame_time = *targets.frame_time;
    *targets.frame_time = paused ? 0.0f : interval;
    current_time = *targets.current_time;
    *targets.current_time = static_cast<float>(tick_base) * interval;
    in_prediction = *targets.in_prediction;
    *targets.in_prediction = 1;
}
void TimeScope::restore() noexcept {
    if (targets.in_prediction) {
        *targets.in_prediction = in_prediction;
        targets.in_prediction = nullptr;
    }
    if (targets.current_time) {
        *targets.current_time = current_time;
        targets.current_time = nullptr;
    }
    if (targets.frame_time) {
        *targets.frame_time = frame_time;
        targets.frame_time = nullptr;
    }
}
CommandBindings bind_command_prediction(const CommandBindings& input, void* const& prediction,
                                        void* player, client::Command& command,
                                        const CommandPlayerOffsets& offsets) noexcept {
    CommandBindings result{prediction,      input.move_helper, input.game_movement, input.move_data,
                           input.flag_head, input.targets,     input.live_convars};
    if (input.live_convars) {
        result.targets.convar_aaa77b8a = input.live_convars->aaa77b8a.get();
        result.targets.convar_de58ce34 = input.live_convars->de58ce34.get();
    }
    result.targets.in_prediction = static_cast<std::uint8_t*>(prediction) + 8;
    result.targets.first_time_predicted = static_cast<std::uint8_t*>(prediction) + 0x18;
    result.targets.command_predicted = &command.predicted;
    result.targets.player_current_command = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(player) + offsets.current_command_f58e6118);
    result.targets.player_field_6610746e = reinterpret_cast<lagcomp::Vector*>(
        static_cast<std::byte*>(player) + offsets.vector_6610746e);
    result.targets.field_a7f8_08 =
        reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(result.game_movement) + 8);
    return result;
}

CommandPrediction::CommandPrediction(const CommandBindings& input, void* player, void* command,
                                     std::uint32_t random_seed)
    : bindings(input),
      saved(input.targets, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player)),
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(command)), random_seed) {
    if (bindings.flag_head)
        set_prediction_flags(*bindings.flag_head, 1);
    engine::invoke<void>(bindings.move_helper, 1, player);
    engine::invoke<void>(bindings.prediction, 20, player, command, bindings.move_helper,
                         bindings.move_data);
    engine::invoke<void>(bindings.game_movement, 1, player, bindings.move_data);
    engine::invoke<void>(bindings.prediction, 21, player, command, bindings.move_data);
    engine::invoke<void>(bindings.move_helper, 1, static_cast<void*>(nullptr));
    engine::invoke<void>(player, 340);
}
void CommandPrediction::restore() noexcept {
    if (!active)
        return;
    if (bindings.flag_head)
        set_prediction_flags(*bindings.flag_head, 0);
    saved.restore();
    active = false;
}
} // namespace skeet::prediction
