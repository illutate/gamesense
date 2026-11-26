#include "skeet/misc/runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace skeet::misc {
using engine::invoke;
namespace {
template <class T> T read(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
const char* command_line_import() {
    return GetCommandLineA();
}
int format_name_import(char* destination, std::size_t capacity, const char* name) {
    return sprintf_s(destination, capacity, "name %s", name);
}
int suffix_import(char* destination, int capacity) {
    return WideCharToMultiByte(CP_UTF8, 0, L"~\u3164~\u3164", -1, destination, capacity, nullptr,
                               nullptr);
}
void print_wins(void* cvar, const char* name, std::int32_t wins) {
    // ICvar::ConsolePrintf is a cdecl variadic entry, including its receiver on
    // the stack. It is not a thiscall despite being stored in a vtable.
    const auto table = *static_cast<void***>(cvar);
    using Function = void (*)(void*, const char*, ...);
    reinterpret_cast<Function>(table[26])(cvar, "    %s: %d\n", name, wins);
}
}

RuntimeImports::RuntimeImports()
    : command_line(command_line_import), format_name_command(format_name_import),
      append_name_suffix(suffix_import) {}
Runtime::Runtime(RuntimeInterfaces& interfaces, AutoAcceptPatchState& patch,
                 client::ChannelHookState& channel_state, RuntimeOffsets offsets)
    : interfaces(interfaces), offsets(offsets), auto_accept_patch(patch), channel(channel_state) {
    error_log.cvar = interfaces.cvar;
    error_log.disabled = interfaces.drawing_suspended;
    error_log.hud_slot = interfaces.log_hud_slot;
    clan_tag.engine = interfaces.engine;
    clan_tag.game_rules = interfaces.game_rules;
    clan_tag.game_phase_offset = offsets.game_phase;
    ping_spike.engine = interfaces.engine;
}

void Runtime::auto_accept_changed() {
    auto& patch = auto_accept_patch.metadata;
    if (!patch.replacement_table || !patch.target)
        return;
    InterlockedExchangePointer(static_cast<void* volatile*>(patch.target),
                               *auto_accept_enabled ? patch.replacement_table
                                                    : patch.original_table);
}

void Runtime::ping_spike_changed(bool enabled) {
    if (!enabled)
        return;
    client::restore_channel_hook(interfaces.engine, channel); //complete shared433CBDFD
}
void Runtime::restore_ping_channel() {
    if (!channel.replacement_table)
        return;
    auto* active = invoke<void*>(interfaces.engine, 78);
    if (!active || *static_cast<void***>(active) != channel.replacement_table)
        return;
    InterlockedExchangePointer(static_cast<void* volatile*>(active), channel.expected_table);
}

void Runtime::steal_player_name() {
    if (!invoke<bool>(interfaces.engine, 26))
        return;
    const auto maximum = *interfaces.max_clients;
    auto* local = interfaces.local_player;
    const auto local_index = invoke<std::int32_t>(static_cast<std::byte*>(local) + 8, 10);
    write<std::uint32_t>(interfaces.name_cvar, 0x60, 0);
    for (std::int32_t attempt = 0; attempt < maximum; ++attempt) {
        // x86 ADD wraps before signed IDIV. This matters for a stale saved index.
        const auto sum = std::uint32_t(last_name_index) + std::uint32_t(attempt);
        const auto signed_sum = static_cast<std::int32_t>(sum);
        const auto index = signed_sum % maximum + 1;
        if (index == local_index)
            continue;
        auto* player = invoke<void*>(interfaces.entity_list, 3, index);
        if (!player || !invoke<bool>(player, 158))
            continue;
        if (read<std::int32_t>(player, offsets.team) != read<std::int32_t>(local, offsets.team))
            continue;
        PlayerInfo info{};
        if (!invoke<bool>(interfaces.engine, 8, index, &info) || info.is_hltv())
            continue;
        std::array<char, 256> command{};
        const auto written =
            imports.format_name_command(command.data(), command.size(), info.name());
        if (written > 0) {
            imports.append_name_suffix(command.data() + written,
                                       static_cast<int>(command.size()) - written);
            command.back() = '\0';
            invoke<void>(interfaces.engine, 108, command.data());
        }
        last_name_index = index;
        return;
    }
}

void Runtime::dump_matchmaking_wins() {
    if (!interfaces.game_rules || !*interfaces.game_rules)
        return;
    if (!read<std::uint8_t>(*interfaces.game_rules, offsets.valve_matchmaking)) {
        error_log.cvar = interfaces.cvar;
        error_log.write("You must be in a Valve matchmaking server.");
        return;
    }
    if (!interfaces.player_resource || !*interfaces.player_resource)
        return;
    auto* resource = *interfaces.player_resource;
    const auto maximum = *interfaces.max_clients;
    for (std::int32_t index = 1; index <= maximum; ++index) {
        auto* player = invoke<void*>(interfaces.entity_list, 3, index);
        if (!player || !invoke<bool>(player, 158))
            continue;
        PlayerInfo info;
        if (!invoke<bool>(interfaces.engine, 8, index, &info) || info.is_hltv())
            continue;
        const auto wins =
            read<std::int32_t>(resource, offsets.competitive_wins + std::uint32_t(index) * 4);
        print_wins(interfaces.cvar, info.name(), wins);
    }
}

void Runtime::upgrade_tablet_changed() {
    const auto* command_line = imports.command_line();
    if (!command_line || !std::strstr(command_line, "-insecure"))
        return;
    if (!*upgrade_tablet_enabled) {
        for (const auto& entry : tablet_flags) {
            auto* parent = read<void*>(entry.variable, 0x1C);
            write(parent, 0x14, entry.flags);
            parent = read<void*>(entry.variable, 0x1C);
            write(parent, 8, entry.flags);
        }
        tablet_flags.clear();
        return;
    }
    if (!tablet_flags.empty())
        return;
    auto* iterator = invoke<void*>(interfaces.cvar, 45);
    invoke<void>(iterator, 0);
    while (invoke<bool>(iterator, 2)) {
        auto* variable = invoke<void*>(iterator, 3);
        if (!invoke<bool>(variable, 1) && (invoke<std::uint32_t>(variable, 5) & 0x4012)) {
            const auto original = invoke<std::uint32_t>(variable, 5);
            tablet_flags.push_back({variable, original});
            auto* parent = read<void*>(variable, 0x1C);
            const auto mask = ~std::uint32_t(0x4012);
            write(parent, 0x14, read<std::uint32_t>(parent, 0x14) & mask);
            parent = read<void*>(variable, 0x1C);
            write(parent, 8, read<std::uint32_t>(parent, 8) & mask);
        }
        invoke<void>(iterator, 1);
    }
    invoke<void>(interfaces.allocator, 5, iterator);
}

} // namespace skeet::misc
