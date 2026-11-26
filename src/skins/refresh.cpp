#include "skeet/skins/refresh.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <limits>

namespace skeet::skins {
namespace {
template <class T> T read_field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write_field(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
}
void WeaponRefresh::invalidate_weapon_models() { // 4334D9E5
    if (!engine::invoke<bool>(bindings_.engine_client, 26))
        return;
    const auto index = engine::invoke<std::int32_t>(bindings_.engine_client, 12);
    const auto player = engine::invoke<void*>(bindings_.entity_list, 3, index);
    if (player && read_field<std::uint8_t>(player, bindings_.life_state) == 0)
        globals_.full_update = 1;
}
float WeaponRefresh::current_time() const {
    return *globals_.current_time;
}
void WeaponRefresh::schedule_full_update(float time) {
    globals_.hud_refresh_at = time;
}
std::int32_t WeaponRefresh::agent_team() const { // 433821B6
    if (!engine::invoke<bool>(bindings_.engine_client, 26))
        return 2;
    if (engine::invoke<bool>(bindings_.engine_client, 82))
        return 2;
    if (engine::invoke<bool>(bindings_.engine_client, 93))
        return 2;
    return bindings_.local_player ? read_field<std::int32_t>(bindings_.local_player, bindings_.team)
                                  : 2;
}
void WeaponRefresh::clear_hud_weapon_selection() { // 433F11C8
    if (!bindings_.hud || !bindings_.find_hud)
        return;
    auto* element = bindings_.find_hud(bindings_.hud, "CCSGO_HudWeaponSelection");
    if (!element)
        return;
    auto* panel = read_field<void*>(element, 0x40);
    if (!panel ||
        reinterpret_cast<std::uintptr_t>(panel) != reinterpret_cast<std::uintptr_t>(element) - 0xA0)
        return;
    auto* entries = read_field<std::byte*>(panel, 0x74);
    if (!entries)
        return;
    const auto count = read_field<std::uint32_t>(panel, 0x80);
    if (count - 1u >= 64u)
        return;
    bool changed = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto* entry = entries + std::size_t(i) * 0x34;
        if (read_field<std::int32_t>(entry, 0x28) != -1) {
            write_field(entry, 0x28, std::int32_t(-1));
            changed = true;
        }
    }
    if (!changed)
        return;
    auto* event = engine::invoke<void*, const char*, bool, std::int32_t>(bindings_.event_manager, 7,
                                                                         "round_start", false, 0);
    engine::invoke<void>(element, 1, event);
    engine::invoke<void>(bindings_.event_manager, 11, event);
}
void WeaponRefresh::process_deferred_hud_refresh() {
    if (globals_.hud_refresh && current_time() >= globals_.hud_refresh_at) {
        globals_.hud_refresh_at = std::numeric_limits<float>::max();
        clear_hud_weapon_selection();
    }
}
} // namespace skeet::skins
