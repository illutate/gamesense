#include "skeet/rage_config.hpp"
#include <stdexcept>

namespace skeet {
namespace {
template <class T>
const T* setting(ConfigRegistries& registries, std::uint32_t hash, ConfigType type) {
    auto* link = registries.descriptors.next ? find_config(registries.descriptors, hash) : nullptr;
    if (!link)
        throw std::invalid_argument("RAGE runtime requires an existing configuration descriptor");
    auto& descriptor = static_cast<ConfigDescriptor&>(*link);
    if (descriptor.type != type)
        throw std::invalid_argument("RAGE runtime configuration has the wrong native type");
    return &static_cast<T&>(descriptor);
}
}
RageRuntimeConfigView::RageRuntimeConfigView(ConfigRegistries& registries)
    : minimum_chance_(setting<IntegerConfig>(registries, 0x3F5E5553, ConfigType::integer)),
      minimum_damage_(setting<IntegerConfig>(registries, 0x18C0E6BE, ConfigType::integer)),
      override_damage_(setting<IntegerConfig>(registries, 0x7DC413FE, ConfigType::integer)),
      double_tap_chance_(setting<IntegerConfig>(registries, 0x3F69C17C, ConfigType::integer)),
      override_enabled_(setting<BooleanConfig>(registries, 0xB80303B4, ConfigType::boolean)),
      delay_shot_(setting<BooleanConfig>(registries, 0xA09C0D52, ConfigType::boolean)),
      selected_hitboxes_(setting<IntegerConfig>(registries, 0x5B900854, ConfigType::integer)),
      avoid_unsafe_(setting<IntegerConfig>(registries, 0xECBBD134, ConfigType::integer)),
      body_disablers_(setting<IntegerConfig>(registries, 0x16C91A85, ConfigType::integer)),
      prefer_safe_(setting<BooleanConfig>(registries, 0xF5A00B3B, ConfigType::boolean)),
      prefer_body_(setting<BooleanConfig>(registries, 0x2B54548D, ConfigType::boolean)) {}
RageThresholdSettings RageRuntimeConfigView::threshold_settings() const noexcept {
    // Native owner 0x434803BC: value offsets +2C,+68,+7C,+B8,+108.
    return {minimum_chance_->value, minimum_damage_->value, override_enabled_->value != 0,
            override_damage_->value, delay_shot_->value != 0};
}
RageThresholds RageRuntimeConfigView::thresholds(std::uint16_t weapon_id,
                                                 std::uint32_t configuration_flags,
                                                 bool override_hotkey_active) const noexcept {
    // Config flag 0x20 selects native global 0x4346A4B0, which is the
    // value of descriptor 0x3F69C17C (Double tap hit chance).
    return prepare_rage_thresholds(threshold_settings(), weapon_id, configuration_flags,
                                   double_tap_chance_->value, override_hotkey_active);
}
RageHitboxMasks RageRuntimeConfigView::hitboxes(std::uint32_t configuration_flags,
                                                bool force_safe_active) const noexcept {
    // Native values 434691CC,43468F78,43467030,43467D70,4346A8D0,
    // consumed directly by 0x433C15E8..0x433C16CC.
    return prepare_rage_hitboxes(std::uint32_t(selected_hitboxes_->value), prefer_safe_->value != 0,
                                 force_safe_active, std::uint32_t(avoid_unsafe_->value),
                                 configuration_flags, prefer_body_->value != 0,
                                 std::uint8_t(body_disablers_->value));
}
void RageRuntimeConfigView::apply_body_disablers(RageBodyAimPolicy& policy) const noexcept {
    apply_rage_body_disablers(policy, std::uint8_t(body_disablers_->value));
}
} // namespace skeet
