#include "skeet/players/state.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/config_storage/presets.hpp"
#include "skeet/misc/runtime.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace skeet::players {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
bool teammates_enabled(const void* variable) {
    for (;;) {
        auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return field<std::uint32_t>(variable, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
}
bool active(const Bindings& bindings) {
    return engine::invoke<bool>(bindings.engine_instance(), 26) && bindings.local_player_instance();
}
}

PlayerList build_list(State& state, const Bindings& bindings, std::int32_t selected_player) {
    PlayerList result;
    if (!active(bindings))
        return result;
    result.active = true;
    result.shared_esp_enabled = bindings.shared_esp_enabled();
    if (bindings.client_limit() > 64)
        throw std::out_of_range("Native player table supports 64 clients");
    for (std::int32_t index = 1; index <= bindings.client_limit(); ++index) {
        auto& record = state.records[std::size_t(index)];
        record.list_row = -1;
        const auto* resource = bindings.resource_instance();
        if (!resource || !field<std::uint8_t>(resource, bindings.connected_offset() + index))
            continue;
        const auto team = field<std::int32_t>(resource, bindings.team_offset() + index * 4);
        if (team != 2 && team != 3)
            continue;
        if (!result.shared_esp_enabled &&
            team == field<std::int32_t>(bindings.local_player_instance(),
                                        bindings.local_team_offset()) &&
            !teammates_enabled(bindings.teammates_variable()))
            continue;
        if (index == engine::invoke<std::int32_t>(bindings.engine_instance(), 12))
            continue;
        misc::PlayerInfo info;
        if (!engine::invoke<bool>(bindings.engine_instance(), 8, index, &info) || info.is_hltv())
            continue;
        std::array<char, 33> name{};
        std::memcpy(name.data(), info.bytes.data() + 0x10, 32);
        for (std::size_t i = 0; i < 32; ++i)
            if (name[i] == '\r' || name[i] == '\n')
                name[i] = ' ';
        const auto row = static_cast<std::uint32_t>(result.entries.size());
        if (selected_player == index)
            result.selected_row = row;
        record.list_row = static_cast<std::int32_t>(row);
        result.entries.push_back({index, config_name_from_utf8(std::string_view(name.data()))});
    }
    return result;
}

bool reset_all(State& state, const Bindings& bindings) {
    if (!active(bindings))
        return false;
    for (std::uint32_t index = 1; index <= 64; ++index)
        if (engine::invoke<void*>(bindings.entity_list_instance(), 3, index) &&
            state.records[index].list_row != -1)
            std::memset(&state.records[index].adjustments, 0, sizeof(Adjustments));
    return true;
}
bool apply_to_all(State& state, const Bindings& bindings, std::uint32_t selected_player) {
    if (!active(bindings) ||
        !engine::invoke<void*>(bindings.entity_list_instance(), 3, selected_player))
        return false;
    if (selected_player >= state.records.size())
        throw std::out_of_range("Invalid selected player index");
    const auto& source = state.records[selected_player].adjustments;
    for (std::uint32_t index = 1; index <= 64; ++index) {
        if (index == selected_player ||
            !engine::invoke<void*>(bindings.entity_list_instance(), 3, index) ||
            state.records[index].list_row == -1)
            continue;
        auto& target = state.records[index].adjustments;
        const auto priority = target.high_priority;
        std::memcpy(&target, &source, sizeof(Adjustments));
        target.high_priority = priority;
    }
    return true;
}

} // namespace skeet::players
