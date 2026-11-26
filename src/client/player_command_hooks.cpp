#include "skeet/client/player_command_hooks.hpp"
#include "skeet/animation/local_helpers.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::client {
namespace {
PlayerCommandHooks* active_runtime;
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class Result, class... Args>
Result original(void* player, std::size_t slot, Args... args) {
    const auto* metadata =
        static_cast<const engine::VtableHookMetadata*>((*static_cast<void***>(player))[-2]);
    return reinterpret_cast<engine::MemberFunction<Result, Args...>>(
        metadata->original_table[slot])(player, args...);
}
}
void PlayerCommandHooks::prediction_gated_callback(void* player) {
    if (field<std::uint8_t>(bindings.prediction, 0x18))
        original<void>(player, 230);
}
std::uint32_t PlayerCommandHooks::ragdoll_result(void* player) {
    if (bindings.effects.disable_ragdolls.value)
        return 0;
    return original<std::uint32_t>(player, 298);
}
void PlayerCommandHooks::publish_animation_command(void* player, std::uintptr_t first,
                                                   std::uintptr_t second) {
    const auto* command = field<const Command*>(player, bindings.fields.current_command_f58e6118);
    if (command) {
        animation::mark_animation_modifiers(
            field<void*>(player, bindings.fields.animation_state_3d52a1a7));
        const auto number = std::uint32_t(command->number);
        prediction_history_storage(*bindings.history).command_tags[number % 150U] = number;
    }
    original<void>(player, 350, first, second);
}

#if defined(_M_IX86) || defined(__i386__)
#define SKEET_PLAYER_COMMAND_ENTRY __fastcall
#else
#define SKEET_PLAYER_COMMAND_ENTRY
#endif
namespace {
void SKEET_PLAYER_COMMAND_ENTRY prediction_entry(void* player, void*) {
    active_runtime->prediction_gated_callback(player);
}
std::uint32_t SKEET_PLAYER_COMMAND_ENTRY ragdoll_entry(void* player, void*) {
    return active_runtime->ragdoll_result(player);
}
void SKEET_PLAYER_COMMAND_ENTRY animation_command_entry(void* player, void*, std::uintptr_t first,
                                                        std::uintptr_t second) {
    active_runtime->publish_animation_command(player, first, second);
}
}
void* PlayerCommandHooks::native_replacement_entry(std::size_t slot, std::uint32_t entry) {
    void* replacement;
    if (entry == 0x4331E412U && slot == 230)
        replacement = reinterpret_cast<void*>(&prediction_entry);
    else if (entry == 0x433AB4B6U && slot == 298)
        replacement = reinterpret_cast<void*>(&ragdoll_entry);
    else if (entry == 0x433D18B0U && slot == 350)
        replacement = reinterpret_cast<void*>(&animation_command_entry);
    else
        throw std::invalid_argument("Unknown captured player-command hook entry/slot");
    return replacement;
}
void PlayerCommandHooks::bind_native_replacement(engine::VtableHookMetadata& metadata,
                                                 std::size_t slot, std::uint32_t entry) {
    auto* replacement = native_replacement_entry(slot, entry);
    active_runtime = this;
    metadata.replacement_table[slot] = replacement;
}
#undef SKEET_PLAYER_COMMAND_ENTRY
} // namespace skeet::client
