#include "skeet/misc/config.hpp"

namespace skeet::misc {
void initialize_fov_config(ConfigRegistries& r, FovConfig& c) noexcept {
    initialize_integer_config(r.descriptors, c.fov, 0xFC225A4Fu, 90);
    initialize_integer_config(r.descriptors, c.zoom_fov, 0xCA80F9EDu, 100);
}
void initialize_knife_config(ConfigRegistries& r, KnifeConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x7A5909EEu);
    initialize_integer_config(r.descriptors, c.mode, 0xA7C81D9Bu, 3);
}
void initialize_zeus_config(ConfigRegistries& r, ZeusConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x2E314CE2u);
}
void initialize_automatic_weapons_config(ConfigRegistries& r, AutomaticWeaponsConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x3D5DE026u);
    initialize_integer_config(r.descriptors, c.delay, 0x3952AAA6u, c.delay.value);
    initialize_hotkey_config(r, c.last_second_defuse, 0x473CC2DEu, 0x52, HotkeyMode::hold);
}
void initialize_quick_switch_config(ConfigRegistries& r, QuickSwitchConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x0D634908u);
}
void initialize_reveal_ranks_config(ConfigRegistries& r, RevealRanksConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x30E0CFE7u);
}
void initialize_reveal_overwatch_config(ConfigRegistries& r, RevealOverwatchConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0xD351D519u);
}
void initialize_auto_accept_config(ConfigRegistries& r, AutoAcceptConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x141F80B7u);
    c.enabled.value = 1;
}
void initialize_clan_tag_config(ConfigRegistries& r, ClanTagConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x9F6C4676u);
}
void initialize_event_log_config(ConfigRegistries& r, EventLogConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.purchases, 0x5086BCF5u);
    initialize_boolean_config(r.descriptors, c.damage, 0x0CE14F5Au);
}
void initialize_grenade_prediction_config(ConfigRegistries& r, GrenadePredictionConfig& c,
                                          esp::GrenadeEspConfig& visual) noexcept {
    initialize_boolean_config(r.descriptors, c.super_toss, 0x37A61386u);
    initialize_boolean_config(r.descriptors, visual.trajectory, 0xDC4C1BB9u);
    initialize_color_config(r.descriptors, visual.trajectory_color, 0x8428F8F9u, 0xB33232FFu);
    initialize_color_config(r.descriptors, visual.trajectory_secondary_color, 0xC2611C1Fu,
                            0xFF3BC896u);
    initialize_boolean_config(r.descriptors, visual.proximity_warning, 0x85BDA341u);
    initialize_integer_config(r.descriptors, visual.proximity_style, 0x85BDA342u, 1);
    initialize_boolean_config(r.descriptors, visual.molotov_area, 0x85BDA343u);
    visual.molotov_area.value = 1;
    initialize_color_config(r.descriptors, visual.molotov_harmful_color, 0x85BDA344u, 0xDC2D4BFFu);
    initialize_color_config(r.descriptors, visual.molotov_safe_color, 0x85BDA345u, 0xB46EFF55u);
    initialize_boolean_config(r.descriptors, visual.smoke_area, 0x85BDA346u);
    initialize_color_config(r.descriptors, visual.smoke_area_color, 0x85BDA347u, 0xB4FFC864u);
    initialize_boolean_config(r.descriptors, c.automatic_release, 0x969EEF1Fu);
    initialize_hotkey_config(r, c.release_key, 0xDB791DB8u, 0x1B, HotkeyMode::always);
    initialize_integer_config(r.descriptors, c.minimum_damage, 0x510F682Du, 50);
}
void initialize_persistent_kill_feed_config(ConfigRegistries& r,
                                            PersistentKillFeedConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x245CEDB5u);
}
void initialize_disable_pure_config(ConfigRegistries& r, DisablePureConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x177AB88Cu);
}
void initialize_rebuy_config(ConfigRegistries& r, RebuyConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x6ED248E1u);
    c.enabled.value = 0;
}
void initialize_console_output_config(ConfigRegistries& r, ConsoleOutputConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x52EADF91u);
}
void initialize_ping_spike_enabled(ConfigRegistries& r, PingSpikeConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x916F47DAu);
}
void initialize_ping_spike_key(ConfigRegistries& r, PingSpikeConfig& c) noexcept {
    initialize_hotkey_config(r, c.key, 0x3C4135FFu, 0x1B, HotkeyMode::hold);
}
void initialize_ping_spike_milliseconds(ConfigRegistries& r, PingSpikeConfig& c) noexcept {
    initialize_integer_config(r.descriptors, c.milliseconds, 0xC5290E81u, 100);
}
void initialize_free_look_config(ConfigRegistries& r, FreeLookConfig& c) noexcept {
    initialize_hotkey_config(r, c.key, 0x77D31850u, 0x1B, HotkeyMode::hold);
}
void initialize_anti_untrusted_config(ConfigRegistries& r, AntiUntrustedConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x851E6ABEu);
    c.enabled.value = 1;
}
void initialize_custom_events_config(ConfigRegistries& r, CustomEventsConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x526A2BF4u);
    c.enabled.value = 1;
}
void initialize_hide_from_obs_config(ConfigRegistries& r, HideFromObsConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0x643364E6u);
}
void initialize_auto_buy_config(ConfigRegistries& r, AutoBuyConfig& c) noexcept {
    initialize_boolean_config(r.descriptors, c.enabled, 0xA81D4E21u);
    initialize_integer_config(r.descriptors, c.primary, 0xA81D4E22u, 0);
    initialize_integer_config(r.descriptors, c.secondary, 0xA81D4E23u, 0);
    initialize_integer_config(r.descriptors, c.gear, 0xA81D4E24u, 0);
}
} // namespace skeet::misc
