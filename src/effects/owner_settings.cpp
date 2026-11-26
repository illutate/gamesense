#include "skeet/effects/owner_settings.hpp"

namespace skeet::effects {
namespace {
void group(ConfigLink& registry, esp::ModelColorGroup& c, std::uint32_t enabled,
           std::uint32_t color_hash, std::uint32_t color, std::uint32_t material_hash,
           std::int32_t material, std::uint32_t secondary_hash, std::uint32_t secondary) {
    initialize_boolean_config(registry, c.enabled, enabled);
    initialize_color_config(registry, c.primary_color, color_hash, color);
    initialize_integer_config(registry, c.material, material_hash, material);
    initialize_color_config(registry, c.secondary_color, secondary_hash, secondary);
}
void occluded(ConfigLink& registry, esp::OccludedModelColorGroup& c, std::uint32_t enabled,
              std::uint32_t color_hash, std::uint32_t color, std::uint32_t behind,
              std::uint32_t behind_color_hash, std::uint32_t behind_color,
              std::uint32_t material_hash, std::int32_t material, std::uint32_t secondary_hash,
              std::uint32_t secondary) {
    initialize_boolean_config(registry, c.enabled, enabled);
    initialize_color_config(registry, c.primary_color, color_hash, color);
    initialize_boolean_config(registry, c.behind_wall, behind);
    initialize_color_config(registry, c.behind_wall_color, behind_color_hash, behind_color);
    initialize_integer_config(registry, c.material, material_hash, material);
    initialize_color_config(registry, c.secondary_color, secondary_hash, secondary);
}
}
void initialize_model_effects_owner_settings(ConfigRegistries& registries, esp::EffectsSettings& e,
                                             esp::ColoredModelsSettings& m) noexcept {
    auto& r = registries.descriptors;
    initialize_integer_config(r, e.impact_duration, 0x4C044F71u, 4);
    initialize_integer_config(r, e.wall_transparency, 0x9B4090D9u, 100);
    initialize_integer_config(r, e.prop_transparency, 0x8BED781Eu, 100);
    initialize_boolean_config(r, m.props, 0xE217C264u);
    initialize_color_config(r, m.props_color, 0x84830790u, 0xFFFFFFFFu);
    //4348E7CA registers this integer without overwriting its stored value.
    initialize_integer_config(r, e.brightness, 0x2E5DD918u, e.brightness.value);
    initialize_color_config(r, e.night_color, 0x2A0B0DF3u, 0xFF2E292Bu);
    initialize_boolean_config(r, e.remove_fog, 0x3359E29Cu);
    initialize_boolean_config(r, e.disable_post_processing, 0xA8321370u);
    occluded(r, m.player, 0xC51E0D3Du, 0xFE06341Du, 0xFF3CC896u, 0xF6B67DE5u, 0xD4699245u,
             0xB2B4783Cu, 0x044F7095u, 0, 0x9FF240E4u, 0xFF00B2FFu);
    occluded(r, m.teammate, 0x1570AF7Cu, 0x2CEFC91Cu, 0xFF323232u, 0x57BF65ECu, 0x04AD8938u,
             0xB2FFFFFFu, 0xE85380FAu, 4, 0xF3D8F0D1u, 0xFF3B2D09u);
    group(r, m.local_player, 0x6670DB4Du, 0x9201E7EDu, 0xFF323232u, 0x3B28B0E5u, 4, 0x9436ECB4u,
          0xFF3B2D09u);
    group(r, m.local_fake, 0x96DB2E1Cu, 0xA98CEAE8u, 0x78000000u, 0x3FC208C1u, 1, 0xF50A4300u,
          0xFF00B2FFu);
    initialize_integer_config(r, m.local_transparency, 0x3EA36C3Au, 0);
    group(r, m.on_shot, 0x5E0E2558u, 0xC2DBF0A4u, 0xFF323232u, 0x17C5B91Au, 5, 0x8B122392u,
          0xFFFFFFFFu);
    initialize_integer_config(r, m.on_shot_duration, 0x1A07B48Fu, 20);
    group(r, m.hands, 0xE8407B64u, 0x5A93F490u, 0xFFFFFFFFu, 0xB7360BA6u, 5, 0x2926F5DEu,
          0xFFA49E54u);
    group(r, m.weapon_viewmodel, 0xDA411D58u, 0x75A868A4u, 0xFF6E1032u, 0x2A1FA11Au, 3, 0x1C678B92u,
          0xFFA49E54u);
    group(r, m.weapons, 0x92EBAD93u, 0x83BE3FA7u, 0xFF6E1032u, 0x118A8443u, 3, 0x6D66C717u,
          0xFFA49E54u);
    initialize_boolean_config(r, m.shadow, 0x67F8885Cu);
    initialize_color_config(r, m.shadow_color, 0xC2140C28u, 0x78000000u);
    initialize_boolean_config(r, e.instant_scope, 0xEAFCEAD9u);
    initialize_boolean_config(r, e.bullet_impacts, 0xD54A1CBDu);
    initialize_boolean_config(r, e.remove_sky, 0xFBD4768Du);
    initialize_integer_config(r, e.force_aspect_ratio, 0xA8F13C40u, 100);
    initialize_integer_config(r, e.third_person_distance, 0xA8F13C41u, 150);
}
} // namespace skeet::effects
