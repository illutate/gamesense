#include "skeet/resolver_runtime/environment_entry.hpp"
#include "skeet/client/player_state.hpp"
#include <cassert>

namespace skeet::resolver_runtime {
bool has_pending_environment_shot(std::span<const lagcomp::HistoryEvaluation> pending,
                                  std::uint32_t selected_index) noexcept {
    for (const auto& shot : pending)
        if ((shot.words[0] & 0x20U) == 0 && shot.state.words[1] == selected_index &&
            (shot.words[0] & 0x40U) != 0)
            return true;
    return false;
}
namespace {
client::PlayerState* prepare(client::PlayerStateCache& cache, const EnvironmentEntryInput& input,
                             const EnvironmentEntryEngine& engine, EnvironmentTarget*& target) {
    if (!engine.enabled_68(engine.context) || input.enabled_cvar == 0.0f || !input.local_exists ||
        input.local_field_9d920e35 != 0 || input.selected_entity == -1)
        return nullptr;
    target = engine.lookup_10(engine.context, input.selected_entity);
    if (target == nullptr || engine.dormant_24(engine.context, *target) ||
        (target->flags_38ca5228 & 8U) != 0 || target->byte_043fa575 != target->byte_334b460c)
        return nullptr;
    const auto index = std::uint32_t(input.selected_entity) & 0xffffU;
    if (has_pending_environment_shot(input.pending_shots, index) || !target->needs_update)
        return nullptr;
    assert(index != 0);
    auto& player = *cache.get(index);
    auto owner = player.environment();
    const LagRecordHistory history{player.history.records, player.history.first,
                                   player.history.count};
    auto update = input.update;
    update.target_origin = target->origin;
    if (!environment_update_ready(owner, history, player.native.resolver_index, update))
        return nullptr;
    return &player;
}
}
client::PlayerState* prepare_environment_update(client::PlayerStateCache& cache,
                                                const EnvironmentEntryInput& input,
                                                const EnvironmentEntryEngine& engine) {
    EnvironmentTarget* target;
    return prepare(cache, input, engine, target);
}
bool update_environment_entry(client::PlayerStateCache& cache, const EnvironmentEntryInput& input,
                              const EnvironmentEntryEngine& gates,
                              const EnvironmentEngine& engine) {
    EnvironmentTarget* target;
    auto* player = prepare(cache, input, gates, target);
    if (player == nullptr)
        return false;
    auto owner = player->environment();
    LagRecordHistory history{player->history.records, player->history.first, player->history.count};
    auto request = input.update;
    request.target_origin = target->origin;
    request.hitboxes = target->hitboxes;
    const bool published = update_environment(owner, history, request, engine);
    player->environment(owner);
    return published;
}
bool update_environment_entry(client::PlayerState& player, const EnvironmentLiveUpdateInput& input,
                              const EnvironmentEngine& engine) {
    if (!environment_update_ready(player, input))
        return false;
    return update_environment(player, input, engine);
}
} // namespace skeet::resolver_runtime
