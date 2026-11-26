#include "skeet/rage_damage.hpp"
#include "rage_math.hpp"
#include <cmath>

namespace skeet {
using namespace rage_detail;

bool hitgroup_armored(std::int32_t armor, std::uint8_t helmet, std::int32_t hitgroup) noexcept {
    if (armor <= 0)
        return false;
    const auto bit = 1U << (std::uint32_t(hitgroup) & 31U);
    if (bit & 0x3dU)
        return true;
    return (bit & 0x102U) && helmet != 0;
}

float hitgroup_damage_scale(std::int32_t armor, std::uint8_t helmet, std::int32_t hitgroup,
                            std::uint16_t weapon_id, float armor_ratio) noexcept {
    if (weapon_id == 0x1fU)
        return 1.0f;
    float scale = hitgroup_multipliers[std::uint32_t(hitgroup) & 15U];
    if (hitgroup_armored(armor, helmet, hitgroup))
        scale *= armor_ratio * 0.5f;
    return scale;
}

std::int32_t final_bullet_damage(float ballistic_damage, float armor_ratio, std::int32_t pellets,
                                 std::uint16_t weapon_id, std::int32_t hitgroup,
                                 std::int32_t target_armor, std::uint8_t target_helmet) noexcept {
    const float scale =
        hitgroup_damage_scale(target_armor, target_helmet, hitgroup, weapon_id, armor_ratio);
    const auto per_pellet = _mm_cvttss_si32(_mm_set_ss(ballistic_damage * scale));
    return signed_word(std::uint32_t(per_pellet) * std::uint32_t(pellets));
}

bool apply_surface_penetration(BulletPenetrationState& state,
                               const SurfacePenetration& surface) noexcept {
    const auto entry_material = surface.entry_surface.material;
    float combined, damage_modifier;
    if (entry_material == 0x47 || entry_material == 0x59) {
        combined = 3.0f;
        damage_modifier = 0.05f;
    } else {
        combined = surface.contents_bit_3 || surface.surface_flag_bit_7
                       ? 1.0f
                       : (surface.exit_surface.penetration_modifier +
                          surface.entry_surface.penetration_modifier) *
                             0.5f;
        damage_modifier = 0.16f;
    }
    if (entry_material == surface.exit_surface.material) {
        if (entry_material == 0x55 || entry_material == 0x57)
            combined = 3.0f;
        else if (entry_material == 0x4c)
            combined = 2.0f;
    }
    const float x = surface.exit[0] - surface.entry[0];
    const float y = surface.exit[1] - surface.entry[1];
    const float z = surface.exit[2] - surface.entry[2];
    // Native performs sqrt followed by squaring; keeping both preserves rounding.
    const float thickness = std::sqrt((x * x + y * y) + z * z);
    const float inverse = maximum_number(0.0f, 1.0f / combined);
    const float weapon_term =
        maximum_number(0.0f, float((3.0 / double(surface.weapon_penetration)) * 1.25));
    const float thickness_term = ((thickness * thickness) * inverse) / 24.0f;
    const float damage_term = state.damage * damage_modifier;
    const float combined_loss =
        float(double(weapon_term) * (double(inverse) * 3.0) + double(damage_term));
    const float lost = maximum_number(0.0f, combined_loss + thickness_term);
    if (!std::isfinite(lost)) {
        state.damage = 0.0f;
        return false;
    }
    state.damage -= lost;
    if (1.0f > state.damage) {
        state.damage = 0.0f;
        return false;
    }
    state.position = surface.exit;
    state.remaining_penetrations = add(state.remaining_penetrations, -1);
    return true;
}

std::int32_t rage_minimum_damage(std::int32_t raw, std::int32_t health, std::uint32_t flags,
                                 float source_factor, std::int32_t source_limit) noexcept {
    if (raw >= 100)
        return add(health, add(raw, -100));
    if (raw != 0)
        return std::min(raw, add(health, 5));
    float candidate = float(std::max(10, add(health, 1))) * source_factor;
    if (!(flags & 4U))
        candidate *= 1.25f;
    candidate = minimum_number(candidate, float(source_limit) - 1.0f);
    candidate = maximum_number(candidate, 1.0f);
    auto effective = _mm_cvttss_si32(_mm_set_ss(candidate + 0.5f));
    const auto shots = shots_to_kill(health, effective);
    if (shots > 1) {
        const auto quartered = signed_word(std::uint32_t(health / shots) << 2);
        effective = std::min(effective, add(quartered / 3, 1));
    }
    return effective;
}
} // namespace skeet
