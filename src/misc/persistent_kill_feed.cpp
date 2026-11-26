#include "skeet/misc/persistent_kill_feed.hpp"
#include "skeet/core.hpp"
#include <cstring>
#include <array>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
void __fastcall update_handler(void* object, std::uint8_t) {
#else
void update_handler(void* object, std::uint8_t) {
#endif
    static_cast<PersistentKillFeedFrameHandler*>(static_cast<engine::FrameUpdateEndpoint*>(object))
        ->runtime.update();
}
const std::array<engine::FrameUpdateEntry, 1> frame_table{update_handler};
template <class T> T field(const void* owner, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(owner) + offset, sizeof value);
    return value;
}
template <class T> void assign(void* owner, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(owner) + offset, &value, sizeof value);
}
}
void* PersistentKillFeedRuntime::death_notice() {
    if (!bindings_.find_hud)
        return nullptr;
    auto* element = bindings_.find_hud(bindings_.hud_context, hud_name_.begin);
    return element ? static_cast<std::byte*>(element) - 0x14 : nullptr;
}
void PersistentKillFeedRuntime::clear(void* owner) {
    const auto previous = field<std::uint8_t>(owner, 0x1c);
    assign(owner, 0x1c, std::uint8_t{0});
    engine::invoke<void>(static_cast<std::byte*>(owner) + 0x14, 7);
    pending_count = 0;
    assign(owner, 0x1c, previous);
}
void PersistentKillFeedRuntime::on_event(events::GameEventReader& event, std::uint32_t hash) {
    if (!settings_.enabled.value)
        return;
    if (engine::invoke<bool>(bindings_.engine, 82))
        return;
    if (engine::invoke<bool>(bindings_.engine, 93))
        return;
    if (hash == events::player_death_hash) {
        events::FieldValue attacker, assister, victim;
        if (event.read(0x455b4dbf, events::FieldType::integer, attacker))
            return;
        if (event.read(0x721dbb88, events::FieldType::integer, assister))
            return;
        if (event.read(0x8361150e, events::FieldType::integer, victim))
            return;
        const auto source = engine::invoke<std::int32_t>(bindings_.engine, 9, attacker.integer);
        const auto assist = engine::invoke<std::int32_t>(bindings_.engine, 9, assister.integer);
        const auto local = engine::invoke<std::int32_t>(bindings_.engine, 12);
        if ((source != local && assist != local) || victim.integer == attacker.integer)
            return;
        auto* hud = death_notice();
        if (!hud)
            return;
        float lifetime = 10000.0f;
        if (bindings_.game_rules && *bindings_.game_rules) {
            const auto* rules = *bindings_.game_rules;
            if (field<std::uint8_t>(rules, bindings_.offsets.rules_d000a932))
                lifetime = 30.0f;
            else {
                const auto duration = static_cast<float>(
                    field<std::int32_t>(rules, bindings_.offsets.rules_972bc124));
                const auto end = duration + field<float>(rules, bindings_.offsets.rules_9f8e7f71);
                const auto remaining = end - field<float>(bindings_.global_vars, 0x10);
                lifetime = maximum_number(1.5f, remaining);
            }
        }
        assign(hud, 0x64, lifetime);
        ++pending_count;
    } else if ((hash == 0x3d07d291 || pending_count > 64) && pending_count) {
        if (auto* hud = death_notice())
            clear(hud);
    }
}
void PersistentKillFeedRuntime::reset() {
    if (auto* hud = death_notice())
        assign(hud, 0x64, 1.5f);
}
void PersistentKillFeedRuntime::update() {
    if (!settings_.enabled.value && pending_count)
        if (auto* hud = death_notice())
            clear(hud);
}
void PersistentKillFeedRuntime::release() {
    if (engine::invoke<bool>(bindings_.engine, 26) && pending_count)
        if (auto* hud = death_notice())
            clear(hud);
}
void PersistentKillFeedRuntime::reset_state() noexcept {
    pending_count = 0;
}
PersistentKillFeedFrameHandler::PersistentKillFeedFrameHandler(engine::OwnerRegistryBinding owners,
                                                               PersistentKillFeedRuntime& concrete)
    : runtime(concrete) {
    engine::register_frame_update(owners, this);
    vtable = frame_table.data();
}
} //namespace skeet::misc
