#include "skeet/esp/frame.hpp"
#include "skeet/esp/world_engine.hpp"
#include "skeet/esp/player_engine.hpp"
#include "detail.hpp"
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
void draw_esp_frame(ui::RenderQueue& queue, EspFrameOwner& owner, std::int32_t& cursor,
                    const EspFrameContext& c) {
    begin_player_frame(owner.frame, c.player_config.activation.value.binding, c.keys,
                       c.players.absolute_frame_time());
    if (c.world_config.bomb.enabled.value && owner.frame.timer > 0.0f &&
        owner.notice_entity != -1) {
        const auto site = c.entities.bomb_site(owner.notice_entity);
        const auto fraction =
            detail::sse_min(detail::sse_max((3.0f - owner.frame.timer) / 3.0f, 0.0f), 1.0f);
        if (c.draw.scripts)
            draw_indicator(*c.draw.scripts, queue, cursor, {&site, 1}, 0xFF69F3FCu,
                           c.indicator_layout, c.entities.indicator_texture(), fraction);
        else
            draw_indicator(queue, cursor, {&site, 1}, 0xFF69F3FCu, c.indicator_layout,
                           c.entities.indicator_texture(), fraction);
    }
    auto* initial_view = effective_view_player(c.players);
    if (!initial_view)
        return;
    const auto game_mode = c.entities.game_type();
    if (game_mode == 6 && c.world_config.danger_zone_items.value)
        reset_danger_zone_models(owner.entity_models);
    WorldEntityScan collected;
    std::array<WorldEntity, 65> dropped_entities;
    const auto highest = c.entities.highest_entity_index();
    const auto world_maximum_players = c.entities.maximum_players();
    for (auto index = world_maximum_players + 1; index < highest; ++index) {
        if (std::uint32_t(index) & 0xFFFF0000u)
            continue;
        auto* entity = c.entities.by_index(index);
        if (!entity)
            continue;
        if (c.entities.dormant(entity))
            continue;
        const auto class_index = c.entities.client_class_index(entity);
        const bool registered = class_index < owner.class_handlers.size() &&
                                owner.class_handlers[class_index] != EntityOverlayHandler::none;
        if (registered) {
            const auto handler = owner.class_handlers[class_index];
            c.entities.dispatch(queue, owner, cursor, c, *initial_view, entity, handler);
            continue;
        }
        const auto scan_options = std::uint32_t(c.world_config.dropped_weapons.value);
        // This branch already captured an absent class handler. The native
        // owner has no deferred specialized vector and does not look it up
        // again after the IsWeapon engine call.
        c.entities.collect_dropped(collected, dropped_entities, entity, index, scan_options, owner,
                                   c);
    }
    draw_dropped_entities(queue, collected, c.world_config, c.dropped, c.entities);
    draw_danger_zone_models(queue, c.draw.projection, game_mode,
                            c.world_config.danger_zone_items.value, owner.entity_models);
    const auto maximum_players = c.players.maximum_players();
    for (std::int32_t slot = 1; slot < maximum_players; ++slot) {
        const bool blocked =
            owner.frame.activation &&
            c.player_adjustments.records[std::uint32_t(slot)].adjustments.disable_visuals != 0;
        const auto prepared =
            prepare_live_player(queue, owner.players, c.player_config, c.world_config, c.players,
                                initial_view, slot, owner.frame.activation, blocked);
        if (prepared.stage == PreparedPlayerStage::observer_panel) {
            draw_live_observer(queue, owner.frame, c.players, initial_view, prepared);
        } else if (prepared.stage == PreparedPlayerStage::overlay) {
            draw_live_prepared_player(queue, c.player_config, c.world_config, prepared, c.draw,
                                      c.players);
        }
    }
}

} // namespace skeet::esp
