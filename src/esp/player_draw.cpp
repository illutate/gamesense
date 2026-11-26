#include "skeet/esp/player_draw.hpp"
#include "skeet/esp/entity_overlay.hpp"
#include "skeet/font_metrics.hpp"
#include "skeet/script/flags.hpp"
#include "detail.hpp"
#include <cmath>
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace detail;
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
PlayerWeaponMetadata weapon_metadata(const RuntimeWeapon& weapon, const PlayerDrawEngine& e) {
    const auto info = e.weapon_information(e.context, weapon);
    PlayerWeaponMetadata output;
    output.definition = weapon.definition;
    output.penetration = info.penetration;
    output.range = info.range;
    output.range_modifier = info.range_modifier;
    output.armor_ratio = e.weapon_armor_ratio(e.context, weapon);
    output.damage = float(e.weapon_damage(e.context, weapon));
    output.kind = info.kind;
    return output;
}

bool needs_box(const PlayerEspConfig& c) noexcept {
    return c.bounding_box.value || c.health_bar.value || c.name.value || c.weapon_text.value ||
           c.weapon_icon.value || c.distance.value || c.flags.value || c.ammo.value;
}
struct ViewPredicate {
    const PlayerRuntimeEngine* engine;
    const RuntimePlayer* player;
};
bool view_is_player(void* raw) {
    const auto& value = *static_cast<ViewPredicate*>(raw);
    return value.engine->is_player(value.engine->context, *value.player);
}
}

float player_reload_cycle(const RuntimePlayer& player, const PlayerReloadBindings& bindings) {
    const auto* descriptor = static_cast<const std::byte*>(player.engine_entity) + bindings.layers;
    if (field<std::int32_t>(descriptor, 0xc) < 1)
        return 1.0f;
    const auto* layer = field<const std::byte*>(descriptor, 0);
    if (field<float>(layer, 0x58) == 0.0f)
        return 1.0f;
    const auto sequence = field<std::int32_t>(layer, 0x50);
    const auto activity = bindings.sequence_activity
                              ? bindings.sequence_activity(player.engine_entity, sequence)
                              : -1;
    return activity == 0x3c7 ? field<float>(layer, 0x64) : 1.0f;
}

std::int32_t overlay_player_health(const RuntimePlayer* player, const PlayerDrawEngine& e,
                                   const PlayerDrawRules& rules) { // 433C652F
    if (!player)
        return 0;
    if (!e.players.dormant(e.players.context, *player))
        return e.player_health(e.context, *player);
    if (!rules.players.present)
        return 0;
    const auto index = e.players.index(e.players.context, *player);
    return rules.players.players[std::uint32_t(index)].connected
               ? rules.dormant_health[std::uint32_t(index)]
               : 0;
}
PlayerWeaponMetadata player_weapon_metadata(const RuntimePlayer& player,
                                            const PlayerDrawState& state,
                                            const PlayerDrawEngine& e) { // 43328CA6
    if (state.active_weapon_handle != -1) {
        if (auto* weapon = e.weapon_by_handle(e.context, state.active_weapon_handle))
            if (ammunition_weapon(weapon->definition))
                return weapon_metadata(*weapon, e);
    }
    if (player.eligibility_state != 0) {
        for (auto handle : state.inventory_handles) {
            if (handle == -1)
                continue;
            auto* weapon = e.weapon_by_handle(e.context, handle);
            if (weapon && e.weapon_is_weapon(e.context, *weapon) &&
                ammunition_weapon(weapon->definition))
                return weapon_metadata(*weapon, e);
        }
    }
    return {};
}
bool armored_hitgroup(i32 armor, bool helmet, i32 hitgroup) noexcept { // 433FC78E
    if (armor <= 0)
        return false;
    const auto bit = std::uint32_t(1) << (std::uint32_t(hitgroup) & 31);
    return (bit & 0x3D) || ((bit & 0x102) && helmet);
}
void set_player_spotted(const RuntimePlayer& player, PlayerDrawState& state, SpottedProxy proxy) {
    if (!proxy)
        return;
    SpottedProxyData packet;
    packet.words[2] = 1;
    packet.words[5] = 1;
    proxy(&packet, player.engine_entity, state.spotted_destination);
}
i32 player_health_number_threshold(const RuntimePlayer& target, const PlayerDrawState& target_state,
                                   const RuntimePlayer& view, const PlayerDrawState& view_state,
                                   const PlayerDrawEngine& e, client::PlayerStateCache& cache) {
    const auto metadata = player_weapon_metadata(view, view_state, e);
    if (metadata.definition != 64)
        return 92;
    const auto index = e.players.index(e.players.context, target);
    const auto& shared = *cache.get(std::uint32_t(index));
    const auto& g = shared.geometry;
    const auto view_origin = e.players.absolute_origin(e.players.context, view);
    const auto x = (view_origin.x + view_state.view_offset.x) - g.origin.x;
    const auto y = (view_origin.y + view_state.view_offset.y) - g.origin.y;
    const auto z = (view_origin.z + view_state.view_offset.z) -
                   (shared.cached_target_height * 0.5f + g.origin.z);
    const auto distance = std::sqrt((x * x + y * y) + z * z);
    const auto damage = float(std::pow(double(metadata.range_modifier), double(distance * 0.002f)) *
                              double(metadata.damage));
    const auto multiplier = armored_hitgroup(target_state.flags.armor, target_state.flags.helmet, 3)
                                ? (metadata.armor_ratio * 0.5f) * 1.25f
                                : 1.25f;
    return truncate(multiplier * damage);
}

std::int32_t bomb_carrier_index(const PlayerFlagGlobals& globals) noexcept {
    if (!globals.game_rules || !*globals.game_rules)
        return -1;
    const auto* rules = static_cast<const std::byte*>(*globals.game_rules);
    if (rules[globals.rules_guard] != std::byte{} || rules[globals.rules_state] != std::byte{} ||
        !globals.player_resource || !*globals.player_resource)
        return -1;
    std::int32_t index;
    std::memcpy(&index,
                static_cast<const std::byte*>(*globals.player_resource) + globals.carrier_index, 4);
    return index;
}

std::uint32_t player_overlay_flags(const RuntimePlayer& player, const PlayerDrawState& state,
                                   const PlayerDrawSnapshotContext& c) {
    const auto& e = c.engine;
    const auto index = e.players.index(e.players.context, player);
    const auto& cached = *c.cache.get(std::uint32_t(index));
    const auto& raw = state.flags;
    auto history = [](const client::PlayerState& entry) {
        RecordHistory result;
        result.count = entry.history.count;
        result.first = entry.history.first;
        if (result.count)
            result.ticks[result.first & 31] =
                entry.history.records[result.first & 31]->header.simulation_tick;
        if (result.count >= 2)
            result.ticks[(result.first + 1) & 31] =
                entry.history.records[(result.first + 1) & 31]->header.simulation_tick;
        const auto* bytes = reinterpret_cast<const std::byte*>(&entry.native);
        std::memcpy(&result.correction_index, bytes + 0x234, 4);
        std::memcpy(&result.correction_count, bytes + 0x238, 4);
        for (unsigned i = 0; i < 6; ++i) {
            std::memcpy(&result.corrections[i].counter, bytes + 0x23c + i * 32, 4);
            std::memcpy(&result.corrections[i].limit, bytes + 0x254 + i * 32, 4);
        }
        return result;
    };
    std::uint32_t flags = 0;
    if (c.rules.players.game_mode != 2) {
        flags = (raw.helmet ? 1u : 0u) | (raw.armor > 0 ? 2u : 0u);
        if (flags == 3)
            flags = 4;
    }
    if (!e.players.dormant(e.players.context, player)) {
        if (raw.scoped)
            flags |= 8;
        if (raw.flash_alpha > 20.0f)
            flags |= 0x10;
        if (raw.vip)
            flags |= 0x80;
        if (player.team == 3 && raw.defusing)
            flags |= 0x100;
        const auto fake_duck_index = e.players.index(e.players.context, player);
        if (fake_duck(history(*c.cache.get(std::uint32_t(fake_duck_index))), raw.duck_amount,
                      raw.duck_speed))
            flags |= 0x200;
        if (state.active_weapon_handle != -1) {
            if (const auto* weapon = e.weapon_by_handle(e.context, state.active_weapon_handle)) {
                if (weapon->ammunition != -1 && player_reload_cycle(player, c.reload) != 1.0f)
                    flags |= 0x20;
                if (e.weapon_kind(e.context, *weapon) == 9 && weapon->pin_pulled)
                    flags |= 0x400;
            }
        }
    }
    const auto carrier = bomb_carrier_index(c.flag_globals);
    if (e.players.index(e.players.context, player) == carrier)
        flags |= 0x40;
    const auto engine_time = e.players.time(e.players.context);
    const auto now = truncate(
        float(static_cast<long double>(engine_time) / static_cast<long double>(raw.tick_interval) +
              0.5L));
    const auto* bytes = reinterpret_cast<const std::byte*>(&cached.native);
    std::int32_t last_hit_tick;
    std::memcpy(&last_hit_tick, bytes + 0x100, 4);
    const auto difference = sub(last_hit_tick, now);
    const auto magnitude = difference < 0 ? sub(0, difference) : difference;
    if (magnitude <= 16)
        flags |= 0x800;
    if (*c.flag_globals.rage_enabled) {
        const auto handle = *c.flag_globals.on_shot_target;
        auto* target = handle == -1 ? nullptr : e.players.by_handle(e.players.context, handle);
        if (target == &player && e.players.local_player &&
            e.players.local_player->life_state == 0 && *c.flag_globals.on_shot_active)
            flags |= 0x1000;
        const auto records = history(cached);
        if (records.count) {
            const auto tick = truncate(raw.simulation_time / raw.simulation_tick_interval + 0.5f);
            if (tick < records.ticks[records.first & 31])
                flags |= 0x20000;
            else if (correction_pending(records))
                flags |= 0x2000;
        }
    }
    return flags;
}

void draw_prepared_player(ui::RenderQueue& queue, const PlayerEspConfig& config,
                          const WorldEspConfig& world, const PreparedPlayer& prepared,
                          PlayerDrawState& state, const PlayerDrawState& view_state,
                          const PlayerDrawSnapshotContext& c) {
    if (prepared.stage != PreparedPlayerStage::overlay)
        return;
    auto& player = *prepared.player;
    auto& geometry = *prepared.geometry;
    const auto& engine = c.engine;
    if (!prepared.dormant && !prepared.teammate) {
        ViewPredicate predicate{&engine.players, prepared.view_player};
        draw_offscreen_indicator(queue, config,
                                 {c.screen, c.view_origin, geometry.origin, c.view_pitch,
                                  c.view_yaw, c.animation_time, c.camera_fov},
                                 {&predicate, view_is_player});
    }
    if (needs_box(config)) {
        RuntimeWeapon* weapon = nullptr;
        if (state.active_weapon_handle != -1)
            weapon = engine.weapon_by_handle(engine.context, state.active_weapon_handle);
        Rectangle box;
        if (player_box(c.projection, geometry.upper, geometry.lower, box)) {
            if (config.health_bar.value) {
                const auto health = overlay_player_health(&player, engine, c.rules);
                const auto threshold = player_health_number_threshold(
                    player, state, *prepared.view_player, view_state, engine, c.cache);
                draw_health(queue, box, health, threshold, config.health_color.value);
            }
            const auto small_height = ui::font_height(*queue.fonts, 4);
            const auto normal_height = ui::font_height(*queue.fonts, 0);
            if (config.name.value) {
                std::string_view name;
                if (engine.player_info(engine.context, prepared.entity_index, name)) {
                    const auto wide = player_name(name);
                    if (!wide.empty())
                        draw_name(queue, box, wide, config.name_color.value, normal_height);
                }
            }
            Point side{add(add(box.x, box.width), 2), box.y};
            auto baseline = add(box.y, box.height);
            if (c.money && !prepared.dormant) {
                auto value = std::u16string(u"$") + decimal(state.money);
                queue.text(side.x, side.y, 0xD215C27Bu, 4, 0, value);
                side.y = add(side.y, small_height);
            }
            float reload = 1.0f;
            if (!prepared.dormant && weapon && weapon->ammunition != -1)
                reload = player_reload_cycle(player, c.reload);
            if (config.flags.value) {
                draw_flags(queue, side, small_height, player_overlay_flags(player, state, c),
                           c.flag_entries);
                if (c.scripts && c.script_flags)
                    script::draw_flags(*c.scripts, *c.script_flags, queue, prepared.entity_index,
                                       side, small_height);
            }
            if (weapon && config.ammo.value && !prepared.dormant && weapon->ammunition != -1) {
                const auto capacity = engine.weapon_capacity(engine.context, *weapon);
                if (capacity > 0)
                    baseline = draw_ammo(queue, box, baseline, weapon->ammunition, capacity, reload,
                                         config.ammo_color.value);
            }
            if (config.distance.value) {
                draw_distance(
                    queue, add(box.x, box.width / 2), baseline, c.view_origin.x - geometry.origin.x,
                    c.view_origin.y - geometry.origin.y, c.view_origin.z - geometry.origin.z);
                baseline = add(baseline, small_height);
            }
            auto icon_baseline = baseline;
            if (config.weapon_text.value) {
                draw_weapon_text(queue, engine.names, prepared.cache_state->cached_definition,
                                 {add(box.x, box.width / 2), add(baseline, 6)}, 0xC8FFFFFFu);
                icon_baseline = add(baseline, small_height);
            }
            if (config.weapon_icon.value && !c.suspended && box.width > 1)
                draw_weapon_icon(queue, engine.icons, prepared.cache_state->cached_definition,
                                 {add(box.x, box.width / 2), add(icon_baseline, 5)},
                                 config.weapon_icon_color.value);
            if (config.bounding_box.value)
                draw_box(queue, box, config.box_color.value);
        }
    }
    if (!prepared.dormant) {
        auto& skeleton = state.skeleton;
        if (config.skeleton.value && skeleton.model_present && skeleton.studio_present) {
            const bool current =
                skeleton.counter_present && skeleton.entity_counter == skeleton.global_counter;
            if (current || engine.setup_bones(engine.context, player, -1, 0x300, c.current_time))
                draw_skeleton(queue, c.projection, skeleton.bones, skeleton.transforms,
                              config.skeleton_color.value);
        }
        if (world.radar.value && !c.suspended && !state.spotted)
            set_player_spotted(player, state, engine.spotted_proxy);
    }
    finish_player_overlay(queue);
}

} // namespace skeet::esp
