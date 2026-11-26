#include "skeet/esp/other_config.hpp"

namespace skeet::esp {
namespace {
void colored(ConfigLink& registry, ColoredWorldItem& item, std::uint32_t enabled_hash,
             std::uint32_t color_hash, std::uint32_t color) noexcept {
    initialize_boolean_config(registry, item.enabled, enabled_hash);
    initialize_color_config(registry, item.color, color_hash, color);
}
}

// Selected descriptors of the native 0x43482632 owner, in constructor order.
void initialize_world_esp_config(ConfigLink& registry, WorldEspConfig& c) noexcept {
    initialize_boolean_config(registry, c.radar, 0xD7CD1B0Bu);
    initialize_boolean_config(registry, c.spectators, 0x38623779u);
    initialize_integer_config(registry, c.dropped_weapons, 0x6D3D6833u, 0);
    initialize_color_config(registry, c.dropped_weapon_color, 0xA10DC407u, 0xC8FFFFFFu);
    colored(registry, c.hostages, 0x559F78DCu, 0xA6B878A8u, 0xC8FC9F38u);
    colored(registry, c.bomb, 0xF2937E07u, 0x2ADA481Bu, 0xFF3CC896u);
    initialize_boolean_config(registry, c.upgrade_tablet, 0x553D3C3Fu);
    initialize_boolean_config(registry, c.danger_zone_items, 0x89653491u);
    colored(registry, c.drone_gun, 0x75A591C2u, 0xDB9A269Au, 0xFF3CC896u);
    colored(registry, c.blackhawk, 0x325AF11Eu, 0x174ABFD6u, 0xFF00E0FFu);
    colored(registry, c.drone, 0x6E6CDAD7u, 0xF407872Bu, 0xFF3CC896u);
    colored(registry, c.random_case, 0x5DA3152Cu, 0x082CE178u, 0xFFF0138Fu);
    colored(registry, c.tool_case, 0xF2A389D2u, 0x928C112Au, 0xFFFFDC00u);
    colored(registry, c.pistol_case, 0x3B6A149Eu, 0xCEE5C756u, 0xFF505050u);
    colored(registry, c.explosive_case, 0xA3B423BEu, 0x55436936u, 0xFF0082FFu);
    colored(registry, c.heavy_weapon_case, 0x52DAAA5Du, 0x72894F7Du, 0xFF0000FFu);
    colored(registry, c.light_weapon_case, 0x081D65D4u, 0xCF605580u, 0xFF3CC896u);
    colored(registry, c.dufflebag, 0xD13FFDF6u, 0x8B5C32AEu, 0xFF3B7FC8u);
    colored(registry, c.jammer, 0x6A586832u, 0x76DA764Au, 0xFF00006Eu);
    colored(registry, c.ammobox, 0xF8774353u, 0x406A4767u, 0xFF3B7FC8u);
    colored(registry, c.armor, 0xFFD5C89Du, 0xC11B6FBDu, 0xFFC8843Bu);
    colored(registry, c.parachute_pack, 0x2802A1F6u, 0xB424B6AEu, 0xFFFFDC00u);
    colored(registry, c.briefcase, 0x2B4CDE08u, 0xC8555DD4u, 0xFF3B7FC8u);
    colored(registry, c.tablet_upgrade_zone, 0x4FD3F678u, 0x58386184u, 0xFFFFF0AAu);
    colored(registry, c.tablet_upgrade_drone, 0x42694ACEu, 0x06D6A3C6u, 0xFFFFF0AAu);
    colored(registry, c.cash_stack, 0x2D915386u, 0xE5F28DFEu, 0xFF00FF00u);
}
void initialize_screen_overlay_config(ConfigLink& registry, ScreenOverlayConfig& c) noexcept {
    // 0x4348DC0B..49, 0x43482503..16, and static descriptor 0x4346A754.
    initialize_boolean_config(registry, c.recoil, 0x78A1B150u);
    initialize_boolean_config(registry, c.inaccuracy, 0xEDF6C582u);
    initialize_color_config(registry, c.inaccuracy_color, 0x2B61D25Au, 0x3C1E1E1Eu);
    initialize_boolean_config(registry, c.penetration_reticle, 0x1E7432A5u);
    initialize_boolean_config(registry, c.crosshair, 0xAD37A2EBu);
    initialize_boolean_config(registry, c.custom_scope, 0xAD37A2ECu);
    initialize_color_config(registry, c.custom_scope_color, 0xAD37A2EDu, 0xFFFFFFFFu);
    initialize_integer_config(registry, c.custom_scope_position, 0xAD37A2EEu, 190);
    initialize_integer_config(registry, c.custom_scope_offset, 0xAD37A2EFu, 15);
    initialize_integer_config(registry, c.custom_scope_speed, 0xAD37A2F0u, 12);
}
void initialize_viewmodel_config(ConfigLink& registry, ViewmodelConfig& c) noexcept {
    initialize_boolean_config(registry, c.enabled, 0x89653492u);
    initialize_integer_config(registry, c.fov, 0x89653493u, 680);
    initialize_integer_config(registry, c.offset_x, 0x89653494u, 25);
    initialize_integer_config(registry, c.offset_y, 0x89653495u, 0);
    initialize_integer_config(registry, c.offset_z, 0x89653496u, -15);
}
void initialize_grenade_esp_config(ConfigLink& registry, GrenadeEspConfig& c) noexcept {
    // Static descriptors 0x43467CC4/0x43468F50 and 0x4348682C..8B.
    initialize_boolean_config(registry, c.entities, 0x25B4B670u);
    initialize_color_config(registry, c.entity_color, 0xB8C2C5BCu, 0);
    initialize_boolean_config(registry, c.trajectory, 0xDC4C1BB9u);
    initialize_color_config(registry, c.trajectory_color, 0x8428F8F9u, 0xB33232FFu);
    initialize_color_config(registry, c.trajectory_secondary_color, 0xC2611C1Fu, 0xFF3BC896u);
    initialize_boolean_config(registry, c.proximity_warning, 0x85BDA341u);
    initialize_integer_config(registry, c.proximity_style, 0x85BDA342u, 1);
    initialize_boolean_config(registry, c.molotov_area, 0x85BDA343u);
    c.molotov_area.value = 1;
    initialize_color_config(registry, c.molotov_harmful_color, 0x85BDA344u, 0xDC2D4BFFu);
    initialize_color_config(registry, c.molotov_safe_color, 0x85BDA345u, 0xB46EFF55u);
    initialize_boolean_config(registry, c.smoke_area, 0x85BDA346u);
    initialize_color_config(registry, c.smoke_area_color, 0x85BDA347u, 0xB4FFC864u);
}
void initialize_shared_esp_config(ConfigRegistries& registry, SharedEspConfig& c) noexcept {
    // 0x43485E26..65; static descriptors 0x43467F88/0x43468588.
    initialize_hotkey_config(registry, c.activation, 0x7174D684u, 0x1B, HotkeyMode::always);
    initialize_boolean_config(registry.descriptors, c.other_team, 0x6834D5DAu);
    initialize_boolean_config(registry.descriptors, c.enabled, 0x0E533025u);
    initialize_boolean_config(registry.descriptors, c.restrict_updates, 0xCBE162F8u);
}
void initialize_other_esp_settings(ConfigRegistries& registry,
                                   OtherEspSettings& settings) noexcept {
    initialize_world_esp_config(registry.descriptors, settings.world);
    initialize_screen_overlay_config(registry.descriptors, settings.overlays);
    initialize_viewmodel_config(registry.descriptors, settings.viewmodel);
    initialize_grenade_esp_config(registry.descriptors, settings.grenades);
    initialize_shared_esp_config(registry, settings.shared);
    initialize_integer_config(registry.descriptors, settings.feature_indicators, 0x59A226C2u, 0);
}

} // namespace skeet::esp
