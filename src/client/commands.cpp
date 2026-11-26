#include "skeet/client/commands.hpp"
#include "skeet/engine/hooks.hpp"
#include <cstring>

namespace skeet::client {

void restore_channel_hook(void* engine_client, ChannelHookState& state) {
    void* const channel = engine::invoke<void*>(engine_client, 78);
    if (channel && state.replacement_table) {
        auto& table = *static_cast<void***>(channel);
        if (table == state.expected_table) {
            table = state.replacement_table;
            state.installed_channel = channel;
        }
    }
}

namespace {
using OriginalCreateMove = engine::MemberFunction<std::uint8_t, float, Command*>;
CommandEntry enter_after_channel_restore(const CommandEntryBindings& bindings, void* player,
                                         float sample_time, Command& command,
                                         OriginalCreateMove original) {
    const std::uint8_t result = original(player, sample_time, &command);
    if (engine::invoke<bool>(bindings.engine_client, 82))
        return {result, CommandPath::engine_return};
    if (engine::invoke<bool>(bindings.engine_client, 93))
        return {result, CommandPath::engine_return};
    if (command.tick != 0)
        return {result, CommandPath::active_command};
    const auto bytes = static_cast<const std::byte*>(player);
    if (bytes[bindings.rejection_bytes[0]] != std::byte{0})
        return {result, CommandPath::engine_return};
    if (bytes[bindings.rejection_bytes[1]] != std::byte{0})
        return {result, CommandPath::engine_return};
    return {result, CommandPath::empty_tick};
}
}
CommandEntry enter_create_move(const CommandEntryBindings& bindings, void* player,
                               float sample_time, Command& command) {
    restore_channel_hook(bindings.engine_client, *bindings.channel_hook);
    return enter_after_channel_restore(
        bindings, player, sample_time, command,
        reinterpret_cast<OriginalCreateMove>(bindings.original_player_table[289]));
}
CommandEntry enter_hooked_create_move(const CommandEntryBindings& bindings, void* player,
                                      float sample_time, Command& command) {
    restore_channel_hook(bindings.engine_client, *bindings.channel_hook);
    auto** current_table = *static_cast<void***>(player);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(current_table[-2]);
    return enter_after_channel_restore(
        bindings, player, sample_time, command,
        reinterpret_cast<OriginalCreateMove>(metadata->original_table[289]));
}

void save_final_command(Command& command) noexcept {
    command.saved_buttons = command.movement.buttons;
    for (std::size_t index = 0; index != 3; ++index)
        std::memcpy(&command.saved_angles[index], &command.angles[index], sizeof(float));
}

bool command_roll_blocked(const ScriptCommandEnvironment& environment) noexcept {
    if (!environment.shared_player || !*environment.shared_player)
        return true;
    const auto* player = static_cast<const std::byte*>(*environment.shared_player);
    return player[environment.player_guard] != std::byte{0};
}

bool command_angles_unrestricted(const ScriptCommandEnvironment& environment) noexcept {
    return *environment.angle_guard == 0 && !command_roll_blocked(environment);
}

} // namespace skeet::client
