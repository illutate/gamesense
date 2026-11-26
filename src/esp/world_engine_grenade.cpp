#include "skeet/esp/world_engine.hpp"
#include "skeet/effects/glow.hpp"
#include "skeet/engine/interface_call.hpp"
#include "world_engine_detail.hpp"
#include <bit>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace world_detail;
void WorldEntityEngine::inferno(ui::RenderQueue& queue, EspFrameOwner& owner,
                                const EspFrameContext& frame, RuntimePlayer& view, void* entity) {
    auto* const viewer = view.engine_entity;
    //43393ECC starts with this unused network index query, even when the
    // following friendly-fire/owner-team gate suppresses the label entirely.
    engine::invoke<std::int32_t>(static_cast<std::byte*>(entity) + 8, 10);
    if (convar_integer(bindings_.friendly_fire) == 0) {
        // Complete433B1A30: resolve the same8D1BD8D0 owner handle used by
        // dropped entities, only after the ConVar's parent chain reads zero.
        const auto handle = field<std::uint32_t>(entity, bindings_.offsets.owner_handle);
        if (handle != UINT32_MAX) {
            auto* thrower = bindings_.queries.grenades().by_handle(handle);
            if (thrower && field<std::int32_t>(thrower, bindings_.offsets.team) ==
                               field<std::int32_t>(viewer, bindings_.offsets.team))
                return;
        }
    }
    grenade(queue, owner, frame, entity);
}
void WorldEntityEngine::grenade(ui::RenderQueue& queue, EspFrameOwner& owner,
                                const EspFrameContext& frame, void* entity) {
    const auto& config = frame.grenade_config;
    if (!config.entities.value)
        return;
    const auto type = static_cast<std::int32_t>(client_class_index(entity));
    const auto& classes = owner.grenade_classes;
    const auto& offsets = bindings_.offsets;
    std::u16string_view label;
    if (type == classes.base_projectile) {
        const auto damage = field<float>(entity, offsets.grenade_damage);
        if (damage == 350.0f && field<float>(entity, offsets.grenade_radius) == 99.0f) {
            if (field<std::int32_t>(entity, offsets.explosion_effect_tick) != 0)
                return;
            label = owner.grenade_labels.explosive;
        } else {
            if (damage != 100.0f || field<float>(entity, offsets.grenade_radius) != 100.0f)
                return;
            label = owner.grenade_labels.flash;
        }
    } else if (type == classes.smoke_projectile)
        label = owner.grenade_labels.smoke;
    else if (type == classes.decoy_projectile)
        label = owner.grenade_labels.decoy;
    else if (type == classes.molotov_projectile || type == classes.inferno)
        label = owner.grenade_labels.fire;
    else
        label = owner.grenade_labels.other;
    Point screen;
    if (!project_point(frame.draw.projection, field<Vector3>(entity, offsets.origin), screen))
        return;
    std::uint32_t color = 0xC8FFFFFFu;
    if (!bindings_.suspended) {
        if (type != classes.inferno && frame.glow && frame.glow->registry) {
            const auto index = field<std::int32_t>(entity, offsets.glow_index);
            if (index != -1) {
                const auto packed = config.entity_color.value;
                constexpr float scale = 0.0039215688593685626983642578125f;
                effects::update_glow(*frame.glow, entity, static_cast<std::uint32_t>(index),
                                     {float(packed & 255) * scale,
                                      float((packed >> 8) & 255) * scale,
                                      float((packed >> 16) & 255) * scale, 1.0f});
            }
        }
        float duration = 0.0f;
        std::int32_t elapsed = 0;
        if (type == classes.smoke_projectile && field<std::uint8_t>(entity, offsets.smoke_active)) {
            duration = 18.0f;
            elapsed = detail::sub(bindings_.current_tick,
                                  field<std::int32_t>(entity, offsets.smoke_start_tick));
        } else if (type == classes.inferno) {
            duration = 7.0f;
            elapsed = detail::sub(bindings_.current_tick,
                                  field<std::int32_t>(entity, offsets.inferno_start_tick));
        }
        if (duration != 0.0f) {
            const float fraction = (duration - float(elapsed) * bindings_.tick_interval) / duration;
            const auto bounded = detail::sse_min(detail::sse_max(fraction, 0.1f), 1.0f);
            const auto alpha =
                std::bit_cast<std::uint32_t>(std::sqrt(bounded) * 255.0f + 8388608.0f) & 255u;
            color = (alpha << 24) | 0xFFFFFFu;
        }
    }
    queue.text(screen.x, detail::add(screen.y, 12), color, 0x14, 0, label);
}
} // namespace skeet::esp
