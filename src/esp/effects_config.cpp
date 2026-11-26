#include "skeet/esp/effects_config.hpp"

namespace skeet::esp {

void initialize_effects_settings(ConfigRegistries& registries, EffectsSettings& s) noexcept {
    auto& r = registries.descriptors;
    initialize_boolean_config(r, s.remove_flash, 0x461D3721u); // static 0x43468FF4
    initialize_boolean_config(r, s.remove_smoke, 0x623BD4F2u); // owner 0x43480354
    initialize_boolean_config(r, s.remove_fog, 0x3359E29Cu);
    initialize_boolean_config(r, s.remove_sky, 0xFBD4768Du);
    initialize_integer_config(r, s.visual_recoil, 0x70BA27E0u, 0); // static 0x4346744C
    initialize_integer_config(r, s.wall_transparency, 0x9B4090D9u, 100);
    initialize_integer_config(r, s.prop_transparency, 0x8BED781Eu, 100);
    initialize_integer_config(r, s.brightness, 0x2E5DD918u, 0);
    initialize_color_config(r, s.night_color, 0x2A0B0DF3u, 0xFF2E292Bu);
    initialize_boolean_config(r, s.remove_scope, 0x171EED00u); // owner 0x4348601F
    initialize_boolean_config(r, s.instant_scope, 0xEAFCEAD9u);
    initialize_boolean_config(r, s.disable_post_processing, 0xA8321370u);
    initialize_boolean_config(r, s.third_person_alive, 0xD35F8540u); // static 0x43468A00
    initialize_hotkey_config(registries, s.third_person_key, 0x3685FCE3u, 0x1B,
                             HotkeyMode::always); // static 0x43468438
    initialize_boolean_config(r, s.third_person_dead, 0x264E3E21u); // static 0x4346DAE0
    initialize_boolean_config(r, s.disable_teammates, 0x4CBB0E86u); // static 0x43468544
    initialize_boolean_config(r, s.disable_ragdolls, 0xB589F662u); // static 0x43469010
    initialize_boolean_config(r, s.bullet_tracers, 0x3A5285A6u);
    initialize_color_config(r, s.tracer_color, 0x635F47DEu, 0xFFFF8296u);
    initialize_boolean_config(r, s.bullet_impacts, 0xD54A1CBDu);
    initialize_integer_config(r, s.impact_duration, 0x4C044F71u, 4);
    initialize_boolean_config(r, s.harmless_molotovs, 0xC82CBD3Bu); // owner 0x43484B04
    initialize_color_config(r, s.harmless_molotov_color, 0xF9200DEFu, 0xFFFF5800u);
    initialize_integer_config(r, s.molotov_percent, 0x921CAA0Au, 65);
    initialize_integer_config(r, s.force_aspect_ratio, 0xA8F13C40u, 100);
    initialize_integer_config(r, s.third_person_distance, 0xA8F13C41u, 150);
}

} // namespace skeet::esp
