#include "skeet/esp/player_engine.hpp"
#include "skeet/esp/entity_overlay.hpp"
#include "skeet/esp/flag_table_initializer.hpp"
#include "skeet/font_metrics.hpp"
#include "skeet/script/flags.hpp"
#include "detail.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::esp {
using namespace detail;
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
RecordHistory history(const client::PlayerState& entry) {
    RecordHistory out;
    out.count = entry.history.count;
    out.first = entry.history.first;
    if (out.count)
        out.ticks[out.first & 31] = entry.history.records[out.first & 31]->header.simulation_tick;
    if (out.count >= 2)
        out.ticks[(out.first + 1) & 31] =
            entry.history.records[(out.first + 1) & 31]->header.simulation_tick;
    const auto* bytes = reinterpret_cast<const std::byte*>(&entry.native);
    out.correction_index = field<std::uint32_t>(bytes, 0x234);
    out.correction_count = field<std::uint32_t>(bytes, 0x238);
    for (unsigned i = 0; i < 6; ++i) {
        out.corrections[i].counter = field<std::int32_t>(bytes, 0x23c + i * 32);
        out.corrections[i].limit = field<std::int32_t>(bytes, 0x254 + i * 32);
    }
    return out;
}
bool needs_box(const PlayerEspConfig& c) {
    return c.bounding_box.value || c.health_bar.value || c.name.value || c.weapon_text.value ||
           c.weapon_icon.value || c.distance.value || c.flags.value || c.ammo.value;
}
PlayerWeaponMetadata metadata(const RuntimeWeapon& weapon, EnginePlayers& source) {
    const auto* info = source.information_pointer(weapon);
    PlayerWeaponMetadata result;
    result.definition = source.definition(weapon);
    result.penetration = field<float>(info, 0x100);
    result.range = field<float>(info, 0x10c);
    result.range_modifier = field<float>(info, 0x110);
    result.armor_ratio = source.armor_ratio(weapon);
    result.damage = float(source.damage(weapon));
    result.kind = field<std::int32_t>(info, 0xfc);
    return result;
}
PlayerWeaponMetadata metadata(const RuntimePlayer& player, EnginePlayers& source) {
    auto handle = source.active_weapon(player);
    if (handle != -1)
        if (auto* weapon = source.weapon(handle))
            if (ammunition_weapon(source.definition(*weapon)))
                return metadata(*weapon, source);
    if (source.eligibility(player))
        for (unsigned index = 0; index < 2; ++index) {
            handle = source.inventory_weapon(player, index);
            if (handle == -1)
                continue;
            auto* weapon = source.weapon(handle);
            if (weapon && source.is_weapon(*weapon) &&
                ammunition_weapon(source.definition(*weapon)))
                return metadata(*weapon, source);
        }
    return {}; //the seven literal words at4346F1B8
}
std::int32_t health(const RuntimePlayer& player, EnginePlayers& source) {
    if (!source.dormant(player))
        return source.health(player);
    auto* resource = source.resource();
    if (!resource)
        return 0;
    const auto index = source.index(player);
    return source.connected(resource, index) ? source.resource_health(resource, index) : 0;
}
std::int32_t threshold(const RuntimePlayer& target, const RuntimePlayer& view,
                       EnginePlayers& source, client::PlayerStateCache& cache) {
    const auto weapon = metadata(view, source);
    if (weapon.definition != 64)
        return 92;
    const auto index = source.index(target);
    const auto& shared = *cache.get(std::uint32_t(index));
    const auto target_origin = shared.geometry.origin;
    const auto target_height = shared.cached_target_height;
    const auto origin = source.origin(view);
    const auto eye = source.view_offset(view);
    const auto x = (origin.x + eye.x) - target_origin.x, y = (origin.y + eye.y) - target_origin.y;
    const auto z = (origin.z + eye.z) - (target_height * 0.5f + target_origin.z);
    const auto distance = std::sqrt((x * x + y * y) + z * z);
    const auto damage = float(std::pow(double(weapon.range_modifier), double(distance * 0.002f)) *
                              double(weapon.damage));
    // Hitgroup3 belongs to mask3D, so433FC78E never reads the helmet byte here.
    const auto multiplier = source.armor(target) > 0 ? (weapon.armor_ratio * 0.5f) * 1.25f : 1.25f;
    return truncate(multiplier * damage);
}
}
PlayerWeaponMetadata player_weapon_metadata(const RuntimePlayer& player, EnginePlayers& source) {
    return metadata(player, source);
}
std::uint32_t player_overlay_flags(const RuntimePlayer& player, const PlayerDrawContext& c,
                                   EnginePlayers& source) {
    const auto index = source.index(player);
    const auto& cached = *c.cache.get(std::uint32_t(index));
    std::uint32_t flags = 0;
    if (source.game_mode() != 2) {
        flags = source.helmet(player) ? 1u : 0u;
        if (source.armor(player) > 0)
            flags |= 2;
        if (flags == 3)
            flags = 4;
    }
    if (!source.dormant(player)) {
        if (source.scoped(player))
            flags |= 8;
        if (source.flash(player) > 20.0f)
            flags |= 0x10;
        if (source.vip(player))
            flags |= 0x80;
        if (source.team(player) == 3 && source.defusing(player))
            flags |= 0x100;
        const auto fake_duck_index = source.index(player);
        const auto records = history(*c.cache.get(std::uint32_t(fake_duck_index)));
        if (records.count >= 2 &&
            sub(records.ticks[records.first & 31], records.ticks[(records.first + 1) & 31]) > 2) {
            const auto amount = source.duck_amount(player);
            if (amount > 0.0f && amount < 1.0f && source.duck_speed(player) == 8.0f)
                flags |= 0x200;
        }
        const auto handle = source.active_weapon(player);
        if (handle != -1)
            if (const auto* weapon = source.weapon(handle)) {
                if (source.ammunition(*weapon) != -1 && source.reload_cycle(player) != 1.0f)
                    flags |= 0x20;
                if (source.kind(*weapon) == 9 && source.pin_pulled(*weapon))
                    flags |= 0x400;
            }
    }
    const auto carrier = source.bomb_carrier();
    if (source.index(player) == carrier)
        flags |= 0x40;
    const auto time = source.time();
    const auto now = truncate(float(
        static_cast<long double>(time) / static_cast<long double>(source.tick_interval()) + 0.5L));
    const auto last = field<std::int32_t>(&cached.native, 0x100);
    const auto difference = sub(last, now),
               magnitude = difference < 0 ? sub(0, difference) : difference;
    if (magnitude <= 16)
        flags |= 0x800;
    if (source.rage_enabled()) {
        const auto handle = source.on_shot_target();
        auto* target = handle == -1 ? nullptr : source.by_handle(handle);
        if (target == &player) {
            auto* local = source.local();
            if (local && source.life(*local) == 0 && source.on_shot_active())
                flags |= 0x1000;
        }
        const auto records = history(cached);
        if (records.count) {
            const auto tick =
                truncate(source.simulation_time(player) / source.simulation_tick_interval() + 0.5f);
            if (tick < records.ticks[records.first & 31])
                flags |= 0x20000;
            else if (correction_pending(records))
                flags |= 0x2000;
        }
    }
    return flags;
}
void EnginePlayers::skeleton(ui::RenderQueue& queue, const Projection& projection,
                             const RuntimePlayer& player, std::uint32_t color) const {
    //433FD30D retains the wrapper; setup may replace its header and bone cache.
    const auto* wrapper =
        field<const std::byte*>(player.engine_entity, offset(0x43467e8c, 0x43467900, 0x32e4daf1));
    if (!wrapper || !field<const void*>(wrapper, 0))
        return;
    if (!bindings_.bone_counter ||
        field<std::int32_t>(player.engine_entity, offset(0x4346a808, 0x43468be8, 0x2b554ad0)) !=
            *bindings_.bone_counter)
        if (!engine::invoke<bool>(static_cast<std::byte*>(player.engine_entity) + 4, 13,
                                  static_cast<Matrix3x4*>(nullptr), -1, 0x300,
                                  *bindings_.current_time))
            return;
    const auto* cache = static_cast<const std::byte*>(player.engine_entity) +
                        offset(0x43469064, 0x4346a484, 0x17978090);
    const auto count = std::min(field<std::int32_t>(field<const void*>(wrapper, 0), 0x9c),
                                field<std::int32_t>(cache, 0xc));
    if (count > 256)
        throw std::length_error("Skeleton exceeds native bone workspace");
    std::array<Point, 256> points;
    std::array<std::uint32_t, 8> projected{};
    for (std::int32_t i = 0; i < count; ++i) {
        const auto* header = field<const std::byte*>(wrapper, 0);
        const auto* bone = header + field<std::int32_t>(header, 0xa0) + std::ptrdiff_t(i) * 0xd8;
        if (!(field<std::uint32_t>(bone, 0xa0) & 0x100))
            continue;
        const auto* matrix = field<const Matrix3x4*>(cache, 0) + i;
        if (!project_point(projection, {matrix->rows[0][3], matrix->rows[1][3], matrix->rows[2][3]},
                           points[std::size_t(i)]))
            continue;
        projected[std::uint32_t(i) >> 5] |= std::uint32_t(1) << (i & 31);
        const auto parent = field<const std::uint32_t*>(wrapper, 0x44)[i];
        if (parent == UINT32_MAX)
            continue;
        if (parent >= 256)
            throw std::out_of_range("Skeleton parent exceeds native bone workspace");
        if (!(projected[parent >> 5] & (std::uint32_t(1) << (parent & 31))))
            continue;
        const auto a = points[parent], b = points[std::size_t(i)];
        queue.line({a.x, a.y, b.x, b.y, color});
    }
}
void draw_live_prepared_player(ui::RenderQueue& queue, const PlayerEspConfig& config,
                               const WorldEspConfig& world, const PreparedPlayer& prepared,
                               const PlayerDrawContext& c, EnginePlayers& source) {
    if (prepared.stage != PreparedPlayerStage::overlay)
        return;
    auto& player = *prepared.player;
    auto& geometry = *prepared.geometry;
    if (!prepared.dormant && !prepared.teammate && config.out_of_fov.value &&
        source.is_player(*prepared.view_player)) {
        const auto screen = queue.screen();
        draw_selected_offscreen_indicator(queue, config,
                                          {screen, source.view_origin(), geometry.origin,
                                           source.view_pitch(), source.view_yaw(),
                                           source.animation_time(), c.camera_fov});
    }
    if (needs_box(config)) {
        RuntimeWeapon* weapon = nullptr;
        const auto handle = source.active_weapon(player);
        if (handle != -1)
            weapon = source.weapon(handle);
        Rectangle box;
        if (player_box(c.projection, geometry.upper, geometry.lower, box)) {
            if (config.health_bar.value) {
                const auto value = health(player, source);
                const auto number = threshold(player, *prepared.view_player, source, c.cache);
                draw_health(queue, box, value, number, config.health_color.value);
            }
            const auto small = ui::font_height(*queue.fonts, 4),
                       normal = ui::font_height(*queue.fonts, 0);
            if (config.name.value) {
                std::string_view name;
                if (source.player_info(prepared.entity_index, name)) {
                    const auto wide = player_name(name);
                    if (!wide.empty())
                        draw_name(queue, box, wide, config.name_color.value, normal);
                }
            }
            Point side{add(add(box.x, box.width), 2), box.y};
            auto baseline = add(box.y, box.height);
            if (c.money && !prepared.dormant) {
                const auto value = std::u16string(u"$") + decimal(source.money(player));
                queue.text(side.x, side.y, 0xd215c27bu, 4, 0, value);
                side.y = add(side.y, small);
            }
            float reload = 1.0f;
            if (!prepared.dormant && weapon && source.ammunition(*weapon) != -1)
                reload = source.reload_cycle(player);
            if (config.flags.value) {
                draw_flags(queue, side, small, player_overlay_flags(player, c, source), c.flags);
                if (c.scripts && c.script_flags)
                    script::draw_flags(*c.scripts, *c.script_flags, queue, prepared.entity_index,
                                       side, small);
            }
            if (weapon && config.ammo.value && !prepared.dormant) {
                const auto ammunition = source.ammunition(*weapon);
                if (ammunition != -1) {
                    const auto capacity = source.capacity(*weapon);
                    if (capacity > 0)
                        baseline = draw_ammo(queue, box, baseline, ammunition, capacity, reload,
                                             config.ammo_color.value);
                }
            }
            if (config.distance.value) {
                const auto& view = source.view_origin();
                draw_distance(queue, add(box.x, box.width / 2), baseline,
                              view.x - geometry.origin.x, view.y - geometry.origin.y,
                              view.z - geometry.origin.z);
                baseline = add(baseline, small);
            }
            auto icon_baseline = baseline;
            if (config.weapon_text.value) {
                draw_weapon_text(queue, c.names, prepared.cache_state->cached_definition,
                                 {add(box.x, box.width / 2), add(baseline, 6)}, 0xc8ffffffu);
                icon_baseline = add(baseline, small);
            }
            if (config.weapon_icon.value && !source.suspended() && box.width > 1)
                draw_weapon_icon(queue, c.icons, prepared.cache_state->cached_definition,
                                 {add(box.x, box.width / 2), add(icon_baseline, 5)},
                                 config.weapon_icon_color.value);
            if (config.bounding_box.value)
                draw_box(queue, box, config.box_color.value);
        }
    }
    if (!prepared.dormant) {
        if (config.skeleton.value)
            source.skeleton(queue, c.projection, player, config.skeleton_color.value);
        if (world.radar.value && !source.suspended() && !source.spotted(player))
            source.set_spotted(player);
    }
    finish_player_overlay(queue);
}
} //namespace skeet::esp
