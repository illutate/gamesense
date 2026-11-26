#include "skeet/anti_aim/config.hpp"

namespace skeet::anti_aim {
namespace {
template <class Value>
void register_descriptor(ConfigLink& registry, ConfigValue<Value>& item, std::uint32_t hash,
                         ConfigType type) noexcept {
    item.hash = hash;
    item.serialized_size = sizeof(Value);
    item.type = type;
    append_config_link(registry, item);
}
template <std::size_t N>
void initialize_offsets(ConfigLink& registry, ConfigValue<std::array<std::int32_t, N>>& item,
                        std::uint32_t hash) noexcept {
    register_descriptor(registry, item, hash, ConfigType::integer_array);
    item.value.fill(0);
}
}
void register_global_config(ConfigRegistries& registries, GlobalConfig& config) noexcept {
    register_descriptor(registries.descriptors, config.enabled, 0x96C0EF29u, ConfigType::boolean);
    register_descriptor(registries.descriptors, config.pitch, 0x6D5F1172u, ConfigType::integer);
    register_descriptor(registries.descriptors, config.yaw, 0xF2D408F3u, ConfigType::integer);
    register_descriptor(registries.descriptors, config.body_yaw, 0x4D6D88C6u, ConfigType::integer);
}
void initialize_owner_config(ConfigRegistries& registries, Config& config) noexcept {
    auto& registry = registries.descriptors;
    initialize_integer_config(registry, config.yaw_base, 0x1D43759Bu, 0);
    initialize_integer_config(registry, config.yaw_jitter, 0x177D7470u, 0);
    initialize_integer_config(registry, config.yaw_jitter_offset, 0xDCA4B692u, 0);
    initialize_boolean_config(registry, config.edge_yaw, 0x8C526C4Fu);
    initialize_boolean_config(registry, config.freestanding, 0x86C04DC4u);
    initialize_offsets(registry, config.body_yaw_offsets, 0x8DFD54BDu);
    initialize_offsets(registry, config.yaw_offsets, 0xFE49974Du);
    initialize_hotkey_config(registries, config.freestanding_hotkey, 0x697E8050u, 0x1Bu,
                             HotkeyMode::always);
    initialize_boolean_config(registry, config.freestanding_body_yaw, 0x2716923Fu);
    initialize_integer_config(registry, config.roll, 0xA1DFC38Du, 0);
    initialize_integer_config(registry, config.custom_pitch, 0x01DE265Eu, 0);
}
void finish_owner_config(Config& config) noexcept {
    config.yaw_offsets.value[1] = -12;
    config.yaw_offsets.value[3] = 80;
}
void initialize_config(ConfigRegistries& registries, Config& config) noexcept {
    initialize_owner_config(registries, config);
    finish_owner_config(config);
}
void initialize_fake_lag_enabled(ConfigRegistries& registries, FakeLagSettings& settings) noexcept {
    initialize_boolean_config(registries.descriptors, settings.enabled, 0xF9BBEC7Bu);
}
void initialize_fake_lag_hotkey(ConfigRegistries& registries, FakeLagSettings& settings) noexcept {
    initialize_hotkey_config(registries, settings.hotkey, 0x27D21BD4u, 0x1Bu, HotkeyMode::always);
}
void initialize_fake_lag_settings(ConfigRegistries& registries,
                                  FakeLagSettings& settings) noexcept {
    initialize_fake_lag_enabled(registries, settings);
    initialize_fake_lag_hotkey(registries, settings);
    initialize_integer_config(registries.descriptors, settings.amount, 0x2A71D5FCu, 0);
    initialize_integer_config(registries.descriptors, settings.variance, 0xCA9768E6u, 0);
    initialize_integer_config(registries.descriptors, settings.limit, 0xF17FEF11u, 13);
}
void initialize_other_global_settings(ConfigRegistries& registries,
                                      OtherGlobalSettings& settings) noexcept {
    initialize_hotkey_config(registries, settings.slow_motion_key, 0x7293DE2Au, 0x14u,
                             HotkeyMode::hold);
    initialize_integer_config(registries.descriptors, settings.leg_movement, 0x8F8A4366u, 0);
}
} // namespace skeet::anti_aim
