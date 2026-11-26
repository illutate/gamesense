#include "skeet/movement_config.hpp"

namespace skeet {

// Descriptor registration order in 0x4348FADB..0x4348FC96 is observable through
// configuration traversal and through the two independently linked hotkeys.
void initialize_movement_config(ConfigRegistries& registries, MovementConfig& config) noexcept {
    auto& descriptors = registries.descriptors;
    initialize_boolean_config(descriptors, config.air_strafe, 0xF2EB89E9u);
    initialize_boolean_config(descriptors, config.config_ad536de0, 0xAD536DE0u);
    initialize_integer_config(descriptors, config.air_strafe_direction, 0x6F6ACC9Bu, 1);
    initialize_integer_config(descriptors, config.air_strafe_smoothing, 0xC94C5F0Bu, 70);
    initialize_boolean_config(descriptors, config.z_hop, 0x3FDF2566u);
    initialize_integer_config(descriptors, config.config_3027dc11, 0x3027DC11u, 175);
    initialize_integer_config(descriptors, config.config_3127de82, 0x3127DE82u, 200);
    initialize_boolean_config(descriptors, config.pre_speed, 0xCA7FC107u);
    initialize_boolean_config(descriptors, config.fast_walk, 0xB5E64AD1u);
    initialize_hotkey_config(registries, config.config_712c1936, 0x712C1936u, 0x46,
                             HotkeyMode::hold);
    initialize_hotkey_config(registries, config.config_2b499e77, 0x2B499E77u, 0x54,
                             HotkeyMode::hold);
    initialize_boolean_config(descriptors, config.easy_strafe, 0xDB2172A1u);
    initialize_boolean_config(descriptors, config.standalone_quick_stop, 0x4CD278CAu);
    initialize_boolean_config(descriptors, config.avoid_collisions, 0x3095C574u);
    initialize_integer_config(descriptors, config.config_af9ae891, 0xAF9AE891u, 200);
    initialize_boolean_config(descriptors, config.slow_motion, 0x4DF394FFu);
    initialize_boolean_config(descriptors, config.bunny_hop, 0xFE3FDAABu);
    initialize_boolean_config(descriptors, config.jump_at_edge, 0x8C13C109u);
}

// 0x43402969..0x434029C5. Runtime state initialization starts at 0x434029C6.
void initialize_quick_peek_config(ConfigRegistries& registries, QuickPeekConfig& config) noexcept {
    initialize_boolean_config(registries.descriptors, config.enabled, 0x141C9037u);
    initialize_hotkey_config(registries, config.hotkey, 0xEC178029u, 0x1B, HotkeyMode::hold);
    initialize_integer_config(registries.descriptors, config.mode, 0x4F67FC33u, 3);
    initialize_color_config(registries.descriptors, config.color, 0xC9FCBE4Bu, 0x4D0000FFu);
    initialize_integer_config(registries.descriptors, config.distance, 0x4CB8E4AAu, 201);
}

} // namespace skeet
