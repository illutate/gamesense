#include "skeet/client/entity_command_owner.hpp"
#include "skeet/prediction/player_simulation.hpp"
#include <bit>

namespace skeet::client {
void reject_entity_prediction(EntityCommandOwner& owner, void* player,
                              const prediction::PlayerCommandContext& context) {
    // 433B2674 reads context +8: the embedded command's number, rather than
    // context +68 or the first word of a standalone Command.
    const auto previous = std::bit_cast<std::int32_t>(std::uint32_t(context.command.number) - 1U);
    const auto& prior = owner.state.history[previous % 150];
    if (prior.command_number == previous)
        repair_entity_prediction_fields(owner, player, prior.state);

    // 433B2A2B..433B2F30 is the same live entity/weapon producer as the finish
    // observer's tail at 43382AE1. Read the context number again after repair,
    // and let that producer preserve existing flags when the slot matches.
    capture_entity_prediction_history(owner, player, context.command.number);
}
} // namespace skeet::client
