#include "skeet/client/input_commands.hpp"
#include <cstring>

namespace skeet::client {
Command& input_command_slot(void* input, std::uint32_t number) noexcept {
    Command* commands;
    std::memcpy(&commands, static_cast<std::byte*>(input) + 0xF0, sizeof commands);
    return commands[number % 150u];
}
const Command& input_command_slot(const void* input, std::uint32_t number) noexcept {
    return input_command_slot(const_cast<void*>(input), number);
}
const Command* find_input_command(const void* input, std::int32_t number) noexcept {
    const auto& command = input_command_slot(input, std::uint32_t(number));
    return command.number == number ? &command : nullptr;
}
} // namespace skeet::client
