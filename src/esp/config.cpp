#include "skeet/esp/config.hpp"

namespace skeet::esp {
void initialize_overlay_config(ConfigLink& registry, OverlayConfig& c) noexcept {
    initialize_boolean_config(registry, c.bounding_box, 0xCFF9EBADu);
    initialize_color_config(registry, c.box_color, 0x4D243E0Du, 0x82FFFFFFu);
    initialize_boolean_config(registry, c.health_bar, 0x49733D3Cu);
    initialize_color_config(registry, c.health_color, 0xD2E84B64u, 0xFF50E178u);
    initialize_boolean_config(registry, c.name, 0xA1C152A1u);
    initialize_color_config(registry, c.name_color, 0x25FEB6C1u, 0xC8FFFFFFu);
    initialize_boolean_config(registry, c.distance, 0x0F6D81D5u);
    initialize_boolean_config(registry, c.flags, 0x084A8B33u);
    initialize_boolean_config(registry, c.ammo, 0x43F02F68u);
    initialize_color_config(registry, c.ammo_color, 0x8BEC37F4u, 0xE6C88C50u);
    initialize_boolean_config(registry, c.out_of_fov, 0x95A660ABu);
    initialize_integer_config(registry, c.arrow_size, 0x4564BFC7u, 12);
    initialize_integer_config(registry, c.arrow_distance, 0x09CBEDAEu, 100);
    initialize_color_config(registry, c.arrow_color, 0x7D18A71Fu, 0xFFFFFFFFu);
}

void initialize_player_esp_config(ConfigRegistries& registries, PlayerEspConfig& c) noexcept {
    auto& registry = registries.descriptors;
    initialize_hotkey_config(registries, c.activation, 0x4700A298u, 0x10, HotkeyMode::always);
    initialize_boolean_config(registry, c.bounding_box, 0xCFF9EBADu);
    initialize_color_config(registry, c.box_color, 0x4D243E0Du, 0x82FFFFFFu);
    initialize_boolean_config(registry, c.health_bar, 0x49733D3Cu);
    initialize_color_config(registry, c.health_color, 0xD2E84B64u, 0xFF50E178u);
    initialize_boolean_config(registry, c.name, 0xA1C152A1u);
    initialize_color_config(registry, c.name_color, 0x25FEB6C1u, 0xC8FFFFFFu);
    initialize_boolean_config(registry, c.weapon_text, 0xBB3FDF6Au);
    initialize_boolean_config(registry, c.weapon_icon, 0x6A874B2Cu);
    initialize_color_config(registry, c.weapon_icon_color, 0x00F64778u, 0xC8FFFFFFu);
    initialize_boolean_config(registry, c.distance, 0x0F6D81D5u);
    initialize_boolean_config(registry, c.glow, 0xB1D008B7u);
    initialize_color_config(registry, c.glow_color, 0xECE872CBu, 0x99783CB4u);
    initialize_boolean_config(registry, c.visualize_sounds, 0xE8D2A207u);
    initialize_color_config(registry, c.sound_color, 0x7E3B4C1Bu, 0xFF3F3FFAu);
    initialize_boolean_config(registry, c.flags, 0x084A8B33u);
    initialize_boolean_config(registry, c.ammo, 0x43F02F68u);
    initialize_color_config(registry, c.ammo_color, 0x8BEC37F4u, 0xE6C88C50u);
    initialize_boolean_config(registry, c.line_of_sight, 0x1DE914E6u);
    initialize_color_config(registry, c.sight_color, 0x745EFA1Eu, 0xFF0000FFu);
    initialize_boolean_config(registry, c.skeleton, 0x523E4B35u);
    initialize_color_config(registry, c.skeleton_color, 0x0E995F95u, 0xFFAAFFFFu);
    initialize_boolean_config(registry, c.teammates, 0x45D9D2ACu);
    initialize_boolean_config(registry, c.out_of_fov, 0x95A660ABu);
    initialize_integer_config(registry, c.arrow_size, 0x4564BFC7u, 12);
    initialize_integer_config(registry, c.arrow_distance, 0x09CBEDAEu, 100);
    initialize_color_config(registry, c.arrow_color, 0x7D18A71Fu, 0xFFFFFFFFu);
    initialize_boolean_config(registry, c.dormant, 0xC410C5D3u);
}
void initialize_hit_marker_config(ConfigLink& registry,
                                  HitMarkerConfig& c) noexcept { // 0x43486537..75
    initialize_boolean_config(registry, c.enabled, 0x9C345DFAu);
    initialize_boolean_config(registry, c.sound, 0x69C9D9F2u);
}
void initialize_aimbot_visualization_config(
    ConfigLink& registry, AimbotVisualizationConfig& c) noexcept { // 0x434804E7..543
    initialize_boolean_config(registry, c.enabled, 0x535095F5u);
    initialize_color_config(registry, c.color, 0x7451E155u, 0xCEE55200u);
    initialize_boolean_config(registry, c.safe_point, 0xE9CC8B95u);
    initialize_color_config(registry, c.safe_point_color, 0xE20F6035u, 0x920000FFu);
}
void initialize_player_esp_settings(ConfigRegistries& registries,
                                    PlayerEspSettings& settings) noexcept {
    initialize_player_esp_config(registries, settings.player);
    initialize_hit_marker_config(registries.descriptors, settings.hit_marker);
    initialize_aimbot_visualization_config(registries.descriptors, settings.aimbot_visualization);
    // Static descriptor at 0x4346906C, linked by 0x43490B75.
    initialize_boolean_config(registries.descriptors, settings.money, 0x53970838u);
}

} // namespace skeet::esp
