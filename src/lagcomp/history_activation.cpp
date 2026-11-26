#include "skeet/lagcomp/history_activation.hpp"
#include "skeet/legit/owner.hpp"

namespace skeet::lagcomp {
bool history_prediction_available(const HistoryActivationBindings& bindings) noexcept {
    if (*bindings.prediction_suppressed != 0)
        return false;
    const auto shared_player = *bindings.local_player;
    if (!shared_player || !*shared_player)
        return false;
    return *(static_cast<const std::byte*>(*shared_player) + bindings.player_prediction_disabled) ==
           std::byte{0};
}

bool history_configuration_active(const HistoryActivationBindings& bindings) noexcept {
    const auto* configuration = legit::legit_owner_settings(*bindings.configuration);
    if (!configuration)
        return false;
    for (std::size_t index = 0; index != 9; ++index) {
        if (configuration->enabled.value[index] ||
            (configuration->trigger_enabled.value[index] &&
             configuration->trigger_reaction.value[index]) ||
            configuration->accuracy_boost.value[index])
            return true;
    }
    return false;
}
} // namespace skeet::lagcomp
