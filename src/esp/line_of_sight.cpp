#include "skeet/esp/line_of_sight.hpp"
#include "skeet/esp/grenade_damage.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/esp/player_engine.hpp"
#include <bit>
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool interpolation_enabled(const void* variable) { //43407DE2
    while (field<const void*>(variable, 0x1c) != variable)
        variable = field<const void*>(variable, 0x1c);
    const auto encoded = field<std::uint32_t>(variable, 0x2c);
    const auto bits = encoded ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
    return std::bit_cast<float>(bits) != 0.0f;
}
}
void draw_line_of_sight(const LineOfSightContext& c) {
    const auto& b = c.bindings;
    if (!c.config.line_of_sight.value || !engine::invoke<bool>(b.engine, 26) || c.suppressed ||
        !c.frame.activation || engine::invoke<bool>(b.engine, 82) ||
        engine::invoke<bool>(b.engine, 93) || !interpolation_enabled(b.interpolation_cvar))
        return;
    auto& players = c.players;
    auto* local = players.local();
    if (!local)
        return;
    const auto maximum = *b.maximum_players;
    for (std::int32_t index = 1; index <= maximum; ++index) {
        auto* player = players.by_index(index);
        if (!player || !players.is_player(*player) || players.life(*player) != 0 ||
            players.health(*player) <= 0 || players.dormant(*player) ||
            players.suppressed(*player) || players.eligibility(*player) ||
            c.adjustments.records[std::uint32_t(index)].adjustments.disable_visuals)
            continue;
        if (!c.config.teammates.value && !live_enemy(local, player, players))
            continue;
        auto start = players.origin(*player);
        start.z = grenade_player_view_height(field<float>(player->engine_entity, b.duck_amount)) +
                  start.z;
        const auto angles = field<Vector3>(player->engine_entity, b.eye_angles);
        constexpr float radians = 0.01745329238474369f;
        const auto yaw = angles.y * radians;
        const auto sy = b.trigonometry.sine(yaw);
        const auto cy = b.trigonometry.cosine(yaw);
        const auto pitch = angles.x * radians;
        const auto sp = b.trigonometry.sine(pitch);
        const auto cp = b.trigonometry.cosine(pitch);
        const auto x = (cp * cy) * 2048.0f + start.x;
        const auto y = (cp * sy) * 2048.0f + start.y;
        const auto z = start.z - sp * 2048.0f;
        const MovementVector delta{x - start.x, y - start.y, z - start.z};
        const bool swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
        const MovementCollisionRay ray{
            {start.x, start.y, start.z}, delta, {0, 0, 0}, {0, 0, 0}, true, swept};
        MovementCollisionTrace hit{};
        trace_movement_world(b.collision, ray, 1, hit, b.trace_flag);
        if (b.debug_overlay) {
            const auto color = c.config.sight_color.value;
            engine::invoke<void>(b.debug_overlay, 20, &start, &hit.end, std::int32_t(color & 255u),
                                 std::int32_t((color >> 8) & 255u),
                                 std::int32_t((color >> 16) & 255u), std::int32_t(color >> 24),
                                 std::int32_t(0), 0.0f);
        }
    }
}
LineOfSightHandler::LineOfSightHandler(engine::OwnerRegistryBinding registry, SoundOverlay& sounds,
                                       EspFrameEvents& events,
                                       events::EventRegistry& event_registry,
                                       const WorldEspConfig& world,
                                       const LineOfSightContext& context)
    : sounds_(sounds), events_(events), event_registry_(event_registry), world_(world),
      context_(context) {
    engine::register_frame_update(registry, this);
    vtable = entries_;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall LineOfSightHandler::update(void* object, std::uint8_t refresh) {
#else
void LineOfSightHandler::update(void* object, std::uint8_t refresh) {
#endif
    auto& self =
        *static_cast<LineOfSightHandler*>(static_cast<engine::FrameUpdateEndpoint*>(object));
    self.sounds_.synchronize_listeners(refresh != 0, self.context_.config, self.world_,
                                       self.events_.dormant_listeners(), self.event_registry_);
    draw_line_of_sight(self.context_);
}
} // namespace skeet::esp
