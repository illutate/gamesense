#include "skeet/rage_policy.hpp"
#include "rage_math.hpp"
#include <cmath>

namespace skeet {
using namespace rage_detail;

RageHitboxMasks prepare_rage_hitboxes(std::uint32_t selected, bool prefer_safe, bool force_safe,
                                      std::uint32_t avoid_unsafe, std::uint32_t configuration_flags,
                                      bool prefer_body, std::uint8_t body_disablers) noexcept {
    RageHitboxMasks result{selected, prefer_safe ? selected : 0U};
    if (force_safe) {
        result.safe |= result.regular;
        result.regular = 0;
    } else if (avoid_unsafe) {
        const auto moved = avoid_unsafe & result.regular;
        result.safe |= moved;
        result.regular &= ~moved;
        if (avoid_unsafe & 0x200U) {
            result.regular &= ~0x200U;
            result.safe &= ~0x200U;
        }
    }
    if (!(configuration_flags & 0x400U) && prefer_body && (body_disablers & 8U))
        result.safe |= 2U;
    return result;
}

void apply_rage_body_disablers(RageBodyAimPolicy& policy, std::uint8_t options) noexcept {
    std::uint32_t reasons = 0;
    if (policy.context_ready && ((policy.hitboxes.regular | policy.hitboxes.safe) & 2U)) {
        if ((options & 2U) && (policy.target_state_flags & 8U))
            reasons |= 2U;
        if ((options & 4U) && (policy.target_state_flags & 0x20U))
            reasons |= 4U;
        if ((options & 8U) && (policy.target_state_flags & 0x80U))
            reasons |= 8U;
        if (reasons & 8U)
            policy.hitboxes.safe |= 2U;
    }
    policy.disabler_reasons = reasons;
    if (!(policy.target_state_flags & 0x80U))
        policy.hitboxes.safe = 0;
}

RageThresholds prepare_rage_thresholds(const RageThresholdSettings& settings,
                                       std::uint16_t weapon_id, std::uint32_t configuration_flags,
                                       std::int32_t forced_chance,
                                       bool override_hotkey_active) noexcept {
    const auto chance = (configuration_flags & 0x20U) ? forced_chance : settings.minimum_chance;
    RageThresholds result{settings.minimum_damage, float(chance) * 0.01f,
                          settings.override_enabled && override_hotkey_active, settings.delay_shot};
    if (weapon_id == 0x1fU)
        result.raw_minimum_damage = std::max(result.raw_minimum_damage, 105);
    if (result.override_active) {
        result.raw_minimum_damage = settings.override_damage;
        result.delay_shot = false;
    }
    return result;
}

} // namespace skeet
