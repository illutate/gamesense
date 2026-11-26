#include "skeet/esp/player_runtime.hpp"
#include "detail.hpp"

// The owner honors MXCSR rounding. In particular, multiplying by +0.6 then
// subtracting must not become multiplication by -0.6 followed by addition.
#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace detail;
namespace {
bool team_enemy(i32 first, i32 second, bool teammates_are_enemies) noexcept {
    if (first == 2)
        return second == 3 || teammates_are_enemies;
    if (first == 3)
        return second == 2 || teammates_are_enemies;
    return false;
}
bool squad_enemy(const RuntimePlayer& first, const RuntimePlayer& second) noexcept {
    return first.squad == -1 || first.squad != second.squad;
}
PlayerGeometry& record(PlayerRuntimeState& state, i32 index) noexcept {
    return state.geometry(std::uint32_t(index));
}
}

RuntimePlayer* effective_view_player(const PlayerRuntimeEngine& engine) { // 43359AFA
    auto* local = engine.local_player;
    if (!local || local->life_state == 0 || local->move_type != 10 ||
        (local->observer_mode != 4 && local->observer_mode != 5) || local->observer_handle == -1)
        return local;
    auto* target = engine.by_handle(engine.context, local->observer_handle);
    return target && engine.is_player(engine.context, *target) ? target : local;
}
bool live_enemy(const RuntimePlayer* first, const RuntimePlayer* second,
                const PlayerRuntimeRules& rules) noexcept { // 4339EF8C
    if (!first || !second || first == second)
        return false;
    if (rules.game_mode == 6)
        return squad_enemy(*first, *second);
    return team_enemy(first->team, second->team, rules.teammates_are_enemies);
}
bool dormant_enemy(const RuntimePlayer* first, const RuntimePlayer* second,
                   const PlayerRuntimeEngine& engine, const PlayerRuntimeRules& rules) { // 433E4E4E
    if (!first || !second || first == second)
        return false;
    if (rules.game_mode == 6)
        return squad_enemy(*first, *second);
    if (!rules.present)
        return false;
    const auto first_index = engine.index(engine.context, *first);
    const auto second_index = engine.index(engine.context, *second);
    const auto& a = rules.players[std::uint32_t(first_index)];
    const auto& b = rules.players[std::uint32_t(second_index)];
    return a.connected && b.connected && team_enemy(a.team, b.team, rules.teammates_are_enemies);
}
bool dormant_alive(const RuntimePlayer* player, const PlayerRuntimeEngine& engine,
                   const PlayerRuntimeRules& rules) { // 433D7563
    if (!player || !rules.present)
        return false;
    const auto index = engine.index(engine.context, *player);
    const auto& entry = rules.players[std::uint32_t(index)];
    return entry.connected && entry.alive;
}
void update_player_geometry(PlayerGeometry& g, const RuntimePlayer& player,
                            const PlayerRuntimeEngine& engine, Vector3 origin) { // 4341AF4A
    g.origin = origin;
    g.center = origin;
    const auto minimum = engine.minimum_bounds(engine.context, player);
    const auto maximum = engine.maximum_bounds(engine.context, player);
    g.center.z += (minimum.z + maximum.z) * 0.5f;
    g.eye_height = player.pose_parameter_1 * 18.0f + 58.0f;
    g.pose_adjustment = player.pose_parameter_12 * 6.0f;
    const auto second_maximum = engine.maximum_bounds(engine.context, player);
    g.lower = {g.center.x, g.center.y, g.center.z - second_maximum.z * 0.6f};
    g.upper = {g.center.x, g.center.y, (g.origin.z + g.eye_height) - g.pose_adjustment};
}

void finish_player_overlay(ui::RenderQueue& queue) {
    queue.set_alpha(1.0f);
    queue.set_desaturation(false);
}
PreparedPlayer prepare_player(ui::RenderQueue& queue, PlayerRuntimeState& state,
                              const PlayerEspConfig& config, const WorldEspConfig& world,
                              const PlayerRuntimeEngine& engine, const PlayerRuntimeRules& rules,
                              PlayerRuntimeTiming timing, RuntimePlayer* initial_view, i32 slot,
                              bool activation, bool blocked) {
    PreparedPlayer output;
    auto reject_slot = [&] {
        record(state, slot).alpha = 0.0f;
        return output;
    };
    if (!activation || blocked)
        return reject_slot();
    auto* player = engine.by_index(engine.context, slot);
    if (!player || player == initial_view || !engine.is_player(engine.context, *player) ||
        player->eligibility_state != 0)
        return reject_slot();
    const auto index = engine.index(engine.context, *player);
    auto& g = record(state, index);
    auto reject = [&] {
        g.alpha = 0.0f;
        return output;
    };
    if (player->suppressed != 0)
        return reject();
    const bool dormant = engine.dormant(engine.context, *player);
    bool teammate, alive;
    if (dormant) {
        if (!rules.present)
            return reject();
        teammate = !dormant_enemy(initial_view, player, engine, rules);
        alive = dormant_alive(player, engine, rules);
    } else {
        teammate = !live_enemy(initial_view, player, rules);
        alive = player->life_state == 0;
        if (alive) {
            const auto origin = engine.absolute_origin(engine.context, *player);
            update_player_geometry(g, *player, engine, origin);
            const auto time = engine.time(engine.context);
            const auto ticks = float(static_cast<long double>(time) /
                                         static_cast<long double>(timing.tick_interval) +
                                     0.5L);
            g.last_live_tick = truncate(ticks);
        } else
            g.alpha = 0.0f;
        if (player->move_type == 10 && (player->observer_mode == 4 || player->observer_mode == 5)) {
            g.alpha = 0.0f;
            if (world.spectators.value) { // constructor owner +0x27C
                output.stage = PreparedPlayerStage::observer_panel;
                output.player = player;
                output.geometry = &g;
                output.entity_index = index;
                output.cache_state = state.cache.find(std::uint32_t(index));
            }
            return output;
        }
    }
    if (!alive || (!config.teammates.value && teammate))
        return reject();
    float alpha;
    if (dormant) {
        if (g.alpha == 0.0f)
            return output;
        if (!rules.retain_dormant_alpha) {
            const auto speed = config.dormant.value ? 0.25f : 4.0f;
            g.alpha = sse_min(sse_max(g.alpha - timing.frame_time * speed, 0.0f), 1.0f);
            if (g.alpha == 0.0f)
                return output;
        }
        queue.set_desaturation(true);
        alpha = sse_min(0.8f, g.alpha);
    } else {
        alpha = sse_min(
            sse_max(g.alpha + timing.frame_time * std::bit_cast<float>(0x41555555u), 0.1f), 1.0f);
        g.alpha = alpha;
    }
    queue.set_alpha(alpha);
    auto* view = effective_view_player(engine);
    if (!view || view == player || !engine.is_player(engine.context, *view)) {
        g.alpha = 0.0f;
        finish_player_overlay(queue);
        return output;
    }
    output = {PreparedPlayerStage::overlay,          player, view, &g, index, dormant, teammate,
              state.cache.find(std::uint32_t(index))};
    return output;
}

void begin_player_frame(PlayerFrameState& state, const HotkeyBinding& binding, InputBitmap input,
                        float absolute_frame_time) {
    state.transient = 0;
    state.activation = hotkey_active(binding, input);
    state.timer = maximum_number(state.timer - absolute_frame_time, 0.0f);
}

char runtime_bomb_site(const PlayerRuntimeEngine& engine, i32 entity_index,
                       const RuntimeBombSites* sites) { // 433AC842
    if (std::uint32_t(entity_index) & 0xFFFF0000u)
        return 'A';
    auto* entity = engine.by_index(engine.context, entity_index);
    if (!entity || !sites)
        return 'A';
    const auto maximum = engine.maximum_bounds(engine.context, *entity);
    const auto minimum = engine.minimum_bounds(engine.context, *entity);
    const Vector3 center{(maximum.x - minimum.x) * 0.5f + minimum.x,
                         (maximum.y - minimum.y) * 0.5f + minimum.y,
                         (maximum.z - minimum.z) * 0.5f + minimum.z};
    auto distance = [&](Vector3 site) {
        const auto x = center.x - site.x, y = center.y - site.y, z = center.z - site.z;
        return (y * y + x * x) + z * z;
    };
    return distance((*sites)[0]) > distance((*sites)[1]) ? 'B' : 'A';
}
void draw_player_frame_notice(ui::RenderQueue& queue, const PlayerFrameState& state,
                              const WorldEspConfig& world, const PlayerRuntimeEngine& engine,
                              i32 entity_index, const RuntimeBombSites* sites, i32& cursor,
                              const IndicatorLayout& layout, i32 texture) {
    if (!world.bomb.enabled.value || !(state.timer > 0.0f) || entity_index == -1)
        return;
    const auto site = runtime_bomb_site(engine, entity_index, sites);
    const auto fraction = sse_min(sse_max((3.0f - state.timer) / 3.0f, 0.0f), 1.0f);
    draw_indicator(queue, cursor, {&site, 1}, 0xFF69F3FCu, layout, texture, fraction);
}

} // namespace skeet::esp
