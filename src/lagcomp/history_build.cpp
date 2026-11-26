#include "skeet/lagcomp/history_build.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::lagcomp {
bool should_build_history_hypotheses(const HistoryHypothesisPolicy& policy, void* player) {
    if (!policy.enabled)
        return false;
    if (policy.game_type == 6) {
        if (policy.local_handle == UINT32_MAX ||
            engine::invoke<void*>(policy.entity_list, 4, policy.local_handle) != player)
            return false;
    }
    if (policy.force)
        return true;
    auto* networkable = static_cast<std::byte*>(player) + 8;
    const auto index = engine::invoke<std::uint32_t>(networkable, 10);
    std::uint32_t priority;
    std::memcpy(&priority, policy.player_table + std::size_t(index) * 0x20 + 0x14, 4);
    if (priority)
        return priority == 2;
    if (hotkey_active(policy.hotkey, policy.keys))
        return true;
    if (policy.settings_mask & policy.active_mode_mask)
        return true;
    return policy.physical_override && (policy.input_flags & 8u);
}
void build_deferred_history_record(LagRecord& record, void* player, float speed_fraction,
                                   const HistoryHypothesisPolicy& policy,
                                   const resolver_runtime::HypothesisInput& input,
                                   resolver_runtime::HypothesisBuildContext& context) {
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    if (!index)
        throw std::logic_error("Deferred history building requires a player index");
    context.entity.entity_flags |= 8u;
    context.entity.ik_mask = UINT32_MAX;
    const auto count = animation::build_resolver_hitbox_matrices(
        context.entity, context.model, context.origin, record.header.animation_yaw, record.matrices,
        context.engine, context.trigonometry);
    if (count)
        record.header.flags |= 4u;
    if (should_build_history_hypotheses(policy, player)) {
        auto parameters = input;
        parameters.speed_fraction = speed_fraction;
        resolver_runtime::build_hypotheses(record, parameters, context);
    }
    context.entity.entity_flags &= ~8u;
}
} // namespace skeet::lagcomp
