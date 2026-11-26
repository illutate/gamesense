#include "skeet/skins/config.hpp"

namespace skeet::skins {
namespace {
void preserved_integer(ConfigLink& registry, IntegerConfig& d, std::uint32_t hash) {
    d.hash = hash;
    d.type = ConfigType::integer;
    d.serialized_size = 4;
    append_config_link(registry, d);
}
}
void Settings::initialize_shared(ConfigRegistries& registries) {
    // 43492E21 only appends the image-initialized 4346A488 descriptor.
    append_config_link(registries.descriptors, filter_by_weapon);
}
void Settings::initialize(ConfigRegistries& registries) {
    auto& r = registries.descriptors;
    initialize_boolean_config(r, mask_t, 0xDE88B6DC);
    preserved_integer(r, mask_model_t, 0x588FC522);
    initialize_boolean_config(r, mask_ct, 0xF6D4D395);
    preserved_integer(r, mask_model_ct, 0x82097AD3);
    initialize_boolean_config(r, glove_t, 0x3A8EF907);
    initialize_boolean_config(r, glove_ct, 0xE5249CA0);
    initialize_integer_config(r, glove_model_t, 0x7439E8B5, 0x13A3);
    initialize_integer_config(r, glove_model_ct, 0x5101537E, 0x13A3);
    preserved_integer(r, glove_paint_t, 0xE13F61E0);
    preserved_integer(r, glove_paint_ct, 0x7CFD0011);
    initialize_boolean_config(r, agent_t, 0x191A5A61);
    initialize_boolean_config(r, agent_ct, 0x6CAF4832);
    initialize_integer_config(r, agent_model_t, 0x3EB3F20B, 0);
    initialize_integer_config(r, agent_model_ct, 0x8191D9BC, 0);
    initialize_boolean_config(r, knife_t, 0xD347BC87);
    initialize_boolean_config(r, knife_ct, 0xD4F2A320);
    preserved_integer(r, knife_model_t, 0xE0BDA969);
    preserved_integer(r, knife_model_ct, 0xB3C4B6CA);
    initialize_boolean_config(r, filter_by_weapon, 0x9AB5333C);
    weapons.hash = 0xB6218FEC;
    weapons.type = ConfigType::integer_array;
    weapons.serialized_size = 0x410;
    append_config_link(r, weapons);
    for (auto& record : weapons.value) {
        record.enabled = record.stattrak = 0;
        record.quality = 100;
        record.seed = record.paintkit = 0;
    }
}
std::int32_t weapon_config_slot(std::int32_t item, std::int32_t team) noexcept {
    if (item <= 0)
        return -1;
    if (item <= 64)
        return item;
    if (std::uint32_t(item - 500) > 25)
        return -1;
    return team == 2 ? 59 : 42;
}
bool synchronize_enabled(const std::array<WeaponSkin, 65>& settings,
                         std::array<std::uint32_t, 32>& enabled) noexcept {
    bool changed = false;
    for (std::uint32_t id = 1; id <= 64; ++id) {
        const auto mask = std::uint32_t(1) << (id & 31);
        const bool old = (enabled[id >> 5] & mask) != 0;
        // Native compares the stored byte with a normalized bit, then bts/btr.
        if (std::uint8_t(old) != settings[id].enabled) {
            if (settings[id].enabled)
                enabled[id >> 5] |= mask;
            else
                enabled[id >> 5] &= ~mask;
            changed = true;
        }
    }
    return changed;
}
} // namespace skeet::skins
