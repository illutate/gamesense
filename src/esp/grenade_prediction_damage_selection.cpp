#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/esp/grenade_damage.hpp"
#include "skeet/rage/targeting.hpp"
#include <algorithm>
#include <immintrin.h>

namespace skeet::esp {
bool consider_grenade_damage(GrenadeDamageSelection& selection, float damage, std::int32_t armor,
                             std::int32_t health, std::int32_t threshold,
                             std::int32_t index) noexcept {
    const auto truncate = [](float value) { return _mm_cvttss_si32(_mm_set_ss(value)); };
    const auto needed = std::min(threshold, armor > 0 ? 50 : 80);
    if (truncate(damage - 3.0f) >= health) {
        selection = {damage, true, index};
        return true;
    }
    if (!(selection.damage >= damage)) {
        selection.damage = damage;
        selection.target_index = index;
        if (needed <= health && needed <= truncate(damage)) {
            selection.reached_threshold = true;
            return true;
        }
    }
    return false;
}
GrenadeDamageSelection select_grenade_damage(Vector3 explosion,
                                             std::span<const GrenadeDamageCandidate> candidates,
                                             std::int32_t maximum_players, std::int32_t threshold,
                                             const rage::TargetBindings& targeting,
                                             GrenadeDamageEngine& engine,
                                             const GrenadeDamageMath& math) {
    GrenadeDamageSelection selected;
    for (std::int32_t index = 1; index < maximum_players; ++index) {
        if (static_cast<std::size_t>(index) >= candidates.size())
            continue;
        const auto& candidate = candidates[index];
        if (!candidate.player || !candidate.entity || !candidate.is_player ||
            candidate.life_state || candidate.field_d070684c || candidate.field_cde290d2 ||
            candidate.field_af649dac || !rage::target_is_enemy(targeting, candidate.entity))
            continue;
        auto player = *candidate.player;
        if (candidate.dormant) {
            if (0.0f >= candidate.history_health)
                continue;
            player.distance_origin = candidate.history_origin;
        } else
            player.distance_origin = player.origin;
        const auto result = evaluate_he_damage(explosion, player, engine, math);
        if (consider_grenade_damage(selected, result.damage, player.armor, candidate.health,
                                    threshold, index))
            break;
    }
    return selected;
}
} // namespace skeet::esp
