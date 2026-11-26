#include "skeet/legit/config.hpp"

namespace skeet::legit {
namespace {
void bytes(ConfigLink& registry, BooleanArrayConfig& descriptor, std::uint32_t hash,
           std::uint8_t value = 0) noexcept {
    descriptor.hash = hash;
    descriptor.serialized_size = 9;
    descriptor.type = ConfigType::integer_array;
    append_config_link(registry, descriptor);
    descriptor.value.fill(value);
}
void keys(ConfigRegistries& registries, HotkeyConfigs& descriptors, std::uint32_t base) noexcept {
    for (std::uint32_t index = 0; index != descriptors.size(); ++index)
        initialize_hotkey_config(registries, descriptors[index], hotkey_hash(base, index), 6,
                                 HotkeyMode::hold);
}
}
void register_settings(ConfigRegistries& r, Settings& s) noexcept {
    auto integer = [&](IntegerArrayConfig& d, std::uint32_t h, std::int32_t v) {
        initialize_integer_array_config(r.descriptors, d, h, v);
    };
    bytes(r.descriptors, s.enabled, 0x0BB660A3);
    integer(s.maximum_fov, 0x1435B790, 100);
    integer(s.speed, 0x9D9E24B1, 65);
    integer(s.attack_speed, 0xC68AC43E, 65);
    integer(s.speed_scale_fov, 0xF7648818, 100);
    integer(s.reaction_time, 0x836DBF4B, 100);
    bytes(r.descriptors, s.through_smoke, 0xE7B78410);
    bytes(r.descriptors, s.while_blind, 0xAFD7D6D3);
    bytes(r.descriptors, s.quick_stop, 0xE9197E9C);
    integer(s.recoil_pitch, 0xA2FBFAC6, 100);
    integer(s.recoil_yaw, 0x1716EAD7, 75);
    integer(s.maximum_lock_time, 0x8946733C, 1001);
    bytes(r.descriptors, s.head, 0xB9327935, 1);
    bytes(r.descriptors, s.chest, 0x6ECF9C72, 1);
    bytes(r.descriptors, s.stomach, 0x771C5252, 1);
    integer(s.first_shot_delay, 0xA09C5083, 0);
    bytes(r.descriptors, s.trigger_enabled, 0xEE182BDA);
    integer(s.minimum_hit_chance, 0x5BDB158D, 75);
    integer(s.minimum_damage, 0x47A24717, 1);
    bytes(r.descriptors, s.burst_fire, 0xD1DC4D24);
    integer(s.burst_time, 0x5BC951E6, 250);
    bytes(r.descriptors, s.config_8a823c30, 0x8A823C30);
    integer(s.config_90b2820a, 0x90B2820A, 200);
    bytes(r.descriptors, s.penetration, 0x6D349307);
    bytes(r.descriptors, s.shoot_smoke, 0xB870359F);
    bytes(r.descriptors, s.shoot_blind, 0x6FDB5A10);
    bytes(r.descriptors, s.trigger_head, 0xBE6224C0, 1);
    bytes(r.descriptors, s.trigger_chest, 0xC8E339F1, 1);
    bytes(r.descriptors, s.trigger_stomach, 0xCFCEE7E5, 1);
    integer(s.trigger_reaction, 0x6CEA053A, 0);
    integer(s.accuracy_boost, 0xFDE7159F, 0);
    integer(s.accuracy_range, 0x2D41A289, 32);
    bytes(r.descriptors, s.standalone_recoil, 0x2BE03F3A);
    keys(r, s.aim_keys, 0xA31AE814);
    keys(r, s.trigger_keys, 0xA53DC8B7);
}
void finish_settings_initialization(Settings& s) noexcept {
    s.head.value[4] = s.head.value[5] = 0;
    s.recoil_pitch.value[5] = s.recoil_yaw.value[5] = 0;
    s.chest.value[5] = s.stomach.value[5] = 1;
}
void initialize_settings(ConfigRegistries& r, Settings& s) noexcept {
    register_settings(r, s);
    finish_settings_initialization(s);
}
} // namespace skeet::legit
