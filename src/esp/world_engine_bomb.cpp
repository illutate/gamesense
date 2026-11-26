#include "skeet/esp/world_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include "world_engine_detail.hpp"
#include <cstdio>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace world_detail;

// 433CD3A7. The registry entity is independent of the world entity whose
// countdown is being drawn. Its trace precedes every target-property read.
float WorldEntityEngine::bomb_damage(EspFrameOwner& owner, void* target) {
    const auto peak = owner.bomb_peak_damage;
    const auto* registry = bindings_.planted_bombs;
    if (!registry || field<std::int32_t>(registry, 0xC) == 0)
        return 0.0f;
    const auto* entries = field<void* const*>(registry, 0);
    auto* bomb = *entries;
    if (!bomb || !field<std::uint8_t>(bomb, bindings_.offsets.bomb_ticking))
        return 0.0f;

    const auto origin = *engine::invoke<const Vector3*>(bomb, 10);
    const MovementVector start{origin.x, origin.y, origin.z + 8.0f};
    const MovementVector delta{start.x - start.x, start.y - start.y, (start.z - 40.0f) - start.z};
    const bool swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
    const MovementCollisionRay ray{start, delta, {0, 0, 0}, {0, 0, 0}, true, swept};
    MovementCollisionTrace trace;
    trace.fraction = 1.0f;
    trace.all_solid = 0;
    trace.start_solid = 0;
    trace.hitgroup = -1;
    trace.entity = 0;
    trace.hitbox = -1;
    bindings_.queries.movement().trace_ray(
        ray, 0x200400Bu,
        {MovementCollisionFilterKind::skip_player,
         static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bomb)), 0, 0},
        trace);
    auto explosion = origin;
    if (trace.fraction != 1.0f) {
        explosion.x = trace.plane_normal.x * 0.6f + trace.end.x;
        explosion.y = trace.plane_normal.y * 0.6f + trace.end.y;
        explosion.z = trace.plane_normal.z * 0.6f + trace.end.z;
    }
    const auto radius = (peak * 3.5f) * std::bit_cast<float>(0x3EAAAAABu);
    const auto denominator = (radius + radius) * radius;
    const auto* target_origin = engine::invoke<const Vector3*>(target, 10);
    const auto explosion_z = explosion.z + 1.0f;
    const auto origin_x = target_origin->x, origin_y = target_origin->y,
               origin_z = target_origin->z;
    const auto offset = field<Vector3>(target, bindings_.offsets.view_offset);
    const auto y = (origin_y + offset.y) - explosion.y;
    const auto x = (origin_x + offset.x) - explosion.x;
    const auto z = (origin_z + offset.z) - explosion_z;
    const auto distance_ratio = ((y * y + x * x) + z * z) / denominator;
    const auto damage = std::exp(-distance_ratio) * peak;
    return field<std::int32_t>(target, bindings_.offsets.armor) > 0 ? damage * 0.5f : damage;
}

// Complete 43345EF2. Raw fields remain behind their original gates; in
// particular the indicator's Lua call precedes life-state, health and damage.
void WorldEntityEngine::planted_bomb(ui::RenderQueue& queue, EspFrameOwner& owner,
                                     std::int32_t& cursor, const EspFrameContext& frame,
                                     RuntimePlayer& view, void* entity) {
    auto* const target = view.engine_entity;
    std::memset(&owner.frame.timer, 0, sizeof owner.frame.timer);
    if (!frame.world_config.bomb.enabled.value)
        return;
    const auto& offsets = bindings_.offsets;
    if (!field<std::uint8_t>(entity, offsets.bomb_ticking))
        return;
    const auto now = bindings_.current_time;
    const auto detonation = field<float>(entity, offsets.bomb_detonation_time);
    const auto remaining = detonation - now;
    auto indicator = [&](std::string_view text, std::uint32_t color, std::int32_t texture) {
        if (frame.draw.scripts)
            draw_indicator(*frame.draw.scripts, queue, cursor, text, color, frame.indicator_layout,
                           texture, -1.0f);
        else
            draw_indicator(queue, cursor, text, color, frame.indicator_layout, texture, -1.0f);
    };
    if (remaining > 0.0f) {
        const auto defuse_end = field<float>(entity, offsets.bomb_defuse_end);
        if (defuse_end > 0.0f && field<std::int32_t>(entity, offsets.bomb_defuser_state) == 1) {
            const bool in_time = !(defuse_end > detonation);
            const auto defuse_remaining = defuse_end - now;
            const auto height = detail::truncate(
                (float(detail::sub(queue.screen().y, 2)) * defuse_remaining) / 10.0f);
            const auto foreground_height = queue.screen().y;
            const auto background_height = queue.screen().y;
            queue.rectangle({0, 0, 20, background_height, 0x73000000u});
            const auto color = (in_time ? 0x7350FF00u : 0x73503200u) | (in_time ? 0x32u : 0xFFu);
            queue.rectangle(
                {1, detail::sub(detail::sub(foreground_height, height), 2), 18, height, color});
        }
        char site = 'A';
        auto* resource_slot = bindings_.queries.grenades().bindings().player_resource_slot;
        if (resource_slot) {
            auto* resource = field<void*>(resource_slot, 0);
            if (resource) {
                const auto origin = field<Vector3>(entity, offsets.bomb_origin);
                const auto first = field<Vector3>(resource, offsets.bomb_site_first);
                const auto second = field<Vector3>(resource, offsets.bomb_site_second);
                if (squared(origin, first) > squared(origin, second))
                    site = 'B';
            }
        }
        char label[48]{};
        const auto length =
            std::snprintf(label, sizeof label, "%c - %.1fs", site, double(remaining));
        if (length > 0)
            indicator({label, std::size_t(length)}, 0xC8FFFFFFu, bindings_.bomb_label_texture);
    }
    if (!(remaining > -1.0f))
        return;
    if (field<std::uint8_t>(target, offsets.life_state))
        return;
    if (engine::invoke<std::int32_t>(target, 122) <= 0)
        return;
    const auto damage = detail::truncate(bomb_damage(owner, target));
    if (!damage)
        return;
    if (damage >= engine::invoke<std::int32_t>(target, 122)) {
        indicator("FATAL", 0xFF3200FFu, -1);
    } else {
        char label[32]{};
        const auto length = std::snprintf(label, sizeof label, "-%d HP", damage);
        if (length > 0)
            indicator({label, std::size_t(length)}, 0xFF69F3FCu, -1);
    }
}
} // namespace skeet::esp
