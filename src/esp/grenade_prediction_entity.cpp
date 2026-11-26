#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/effects/glow.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>

namespace skeet::esp {
void draw_inferno_entity(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                         const Projection& projection, const GrenadeEntityClasses& classes,
                         const GrenadeEntityLabels& labels, const GrenadeEntityState& state,
                         std::int32_t current_tick, float tick_interval, bool suspended,
                         effects::GlowEngine* glow, const InfernoTeamState& teams) {
    if (!teams.friendly_fire && teams.owner_present && teams.owner_team == teams.local_team)
        return;
    draw_grenade_entity(queue, config, projection, classes, labels, state, current_tick,
                        tick_interval, suspended, glow);
}
void draw_grenade_entity(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                         const Projection& projection, const GrenadeEntityClasses& classes,
                         const GrenadeEntityLabels& labels, const GrenadeEntityState& state,
                         std::int32_t current_tick, float tick_interval, bool suspended,
                         effects::GlowEngine* glow) {
    if (!config.entities.value)
        return;
    std::u16string_view label;
    if (state.class_index == classes.base_projectile) {
        if (state.damage == 350.0f && state.damage_radius == 99.0f) {
            if (state.explosion_effect_tick != 0)
                return;
            label = labels.explosive;
        } else {
            if (state.damage != 100.0f || state.damage_radius != 100.0f)
                return;
            label = labels.flash;
        }
    } else if (state.class_index == classes.smoke_projectile)
        label = labels.smoke;
    else if (state.class_index == classes.decoy_projectile)
        label = labels.decoy;
    else if (state.class_index == classes.molotov_projectile ||
             state.class_index == classes.inferno)
        label = labels.fire;
    else
        label = labels.other;
    Point screen;
    if (!project_point(projection, state.origin, screen))
        return;
    auto color = 0xc8ffffffu;
    if (!suspended) {
        if (state.class_index != classes.inferno && glow && state.glow_index != -1) {
            const auto packed = config.entity_color.value;
            const auto scale = std::bit_cast<float>(0x3b808081u);
            effects::update_glow(*glow, state.entity, static_cast<std::uint32_t>(state.glow_index),
                                 {static_cast<float>(packed & 255) * scale,
                                  static_cast<float>((packed >> 8) & 255) * scale,
                                  static_cast<float>((packed >> 16) & 255) * scale, 1.0f});
        }
        float duration = 0;
        std::int32_t start = 0;
        if (state.class_index == classes.smoke_projectile && state.smoke_effect) {
            duration = 18.0f;
            start = state.smoke_start_tick;
        } else if (state.class_index == classes.inferno) {
            duration = 7.0f;
            start = state.inferno_start_tick;
        }
        if (duration != 0) {
            const auto elapsed =
                std::bit_cast<std::int32_t>(std::uint32_t(current_tick) - std::uint32_t(start));
            const auto fraction =
                (duration - static_cast<float>(elapsed) * tick_interval) / duration;
            const auto bounded = _mm_cvtss_f32(
                _mm_min_ss(_mm_max_ss(_mm_set_ss(fraction), _mm_set_ss(0.1f)), _mm_set_ss(1.0f)));
            const auto alpha =
                std::bit_cast<std::uint32_t>(std::sqrt(bounded) * 255.0f + 8388608.0f) & 255u;
            color = (alpha << 24) | 0xffffffu;
        }
    }
    queue.text(screen.x, std::bit_cast<std::int32_t>(std::uint32_t(screen.y) + 12u), color, 0x14, 0,
               label);
}
} // namespace skeet::esp
