#include "skeet/skins/agents.hpp"
#include <bit>

namespace skeet::skins {
void apply_agent_model(const Settings& settings, const AgentModelState& state, AgentEntity* local,
                       AgentEngine& engine, ModelInfo& models, StringTables& tables) {
    if (state.original_model == -1 || !local)
        return;
    if (engine.demo_playback() || engine.hltv())
        return;
    bool active = false;
    std::uint32_t selected = 0;
    if (local->team == 2) {
        active = settings.agent_t.value != 0;
        selected = std::uint32_t(settings.agent_model_t.value);
    } else if (local->team == 3) {
        active = settings.agent_ct.value != 0;
        selected = std::uint32_t(settings.agent_model_ct.value);
    }
    if (active && !local->dormant) {
        const auto paths = engine.agent_paths(local->team);
        if (selected >= paths.size())
            return;
        const auto path = paths[selected];
        const auto index = resolve_model_index(path, models, tables);
        if (index == -1)
            return;
        if (auto support = engine.support_models(path))
            for (auto model : *support)
                resolve_model_index(model, models, tables);
        auto* target = local;
        if (local->dead) {
            if (local->ragdoll_handle == 0xFFFFFFFF)
                return;
            target = engine.ragdoll(local->ragdoll_handle);
            if (!target)
                return;
        }
        target->model_index = std::bit_cast<std::int16_t>(std::uint16_t(index));
        return;
    }
    if (local->handle == state.original_handle)
        local->model_index = std::bit_cast<std::int16_t>(std::uint16_t(state.original_model));
}
} // namespace skeet::skins
