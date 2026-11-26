#include "owner_detail.hpp"

namespace skeet::rage::owner_detail {
void initialize_settings(ConfigRegistries& r, AimbotSettings& a,
                         esp::AimbotVisualizationConfig& v) noexcept {
    auto& d = r.descriptors;
    initialize_integer_config(d, a.minimum_hit_chance, 0x3F5E5553, 50);
    initialize_integer_config(d, a.maximum_fov, 0xD6D22A5A, 180);
    initialize_integer_config(d, a.target_selection, 0x65E86322, 0);
    initialize_integer_config(d, a.minimum_damage, 0x18C0E6BE, 10);
    initialize_boolean_config(d, a.damage_override, 0xB80303B4);
    initialize_hotkey_config(r, a.damage_override_key, 0x7EB3A142, 0x1B, HotkeyMode::hold);
    initialize_integer_config(d, a.override_damage, 0x7DC413FE, 10);
    initialize_integer_config(d, a.multipoint_scale, 0xA25029BA, 90);
    initialize_integer_config(d, a.multipoint, 0xBFABB4A0, 0);
    initialize_integer_config(d, a.accuracy_boost, 0x4346EDB5, 1);
    initialize_boolean_config(d, a.delay_shot, 0xA09C0D52);
    initialize_boolean_config(d, v.enabled, 0x535095F5);
    initialize_color_config(d, v.color, 0x7451E155, 0xCEE55200);
    initialize_boolean_config(d, v.safe_point, 0xE9CC8B95);
    initialize_color_config(d, v.safe_point_color, 0xE20F6035, 0x920000FF);
    initialize_boolean_config(d, a.force_body_aim_on_peek, 0x9BBD48EA);
}
}
