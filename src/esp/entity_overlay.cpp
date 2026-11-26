#include "skeet/esp/entity_overlay.hpp"
#include "skeet/esp/world_engine.hpp"
#include "skeet/font_metrics.hpp"
#include "detail.hpp"
#include <stdexcept>

namespace skeet::esp {
using namespace detail;
namespace {
float distance_squared(Vector3 a, Vector3 b) noexcept {
    const auto x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return (y * y + x * x) + z * z;
}
void distance_label(ui::RenderQueue& q, Point at, std::uint32_t color, float squared) {
    auto text = decimal(mul(sub(2, truncate(std::sqrt(squared) / -12.0f)) / 5, 5));
    text += u" FT";
    q.text(at.x, at.y, color, 0x14, 0, text);
}
Vector3 transform(const Matrix3x4& m, Vector3 v, bool second = false) noexcept {
    return {((v.x * m.rows[0][0] + v.y * m.rows[0][1]) + v.z * m.rows[0][2]) + m.rows[0][3],
            ((v.y * m.rows[1][1] + v.x * m.rows[1][0]) + v.z * m.rows[1][2]) + m.rows[1][3],
            second
                ? ((v.z * m.rows[2][2] + v.x * m.rows[2][0]) + v.y * m.rows[2][1]) + m.rows[2][3]
                : ((v.x * m.rows[2][0] + v.y * m.rows[2][1]) + v.z * m.rows[2][2]) + m.rows[2][3]};
}
}
std::uint32_t entity_class_hash(std::string_view name) noexcept {
    std::uint32_t hash = 0x811C9DC5u;
    for (const auto byte : name) {
        if (!byte)
            break;
        hash = (hash ^ std::uint8_t(byte)) * 0x01000193u;
    }
    return hash;
}
EntityOverlayHandler entity_overlay_handler(std::uint32_t hash) noexcept {
    switch (hash) {
    case 0x1A122BDF:
        return EntityOverlayHandler::hostage;
    case 0x1D49B0B5:
        return EntityOverlayHandler::dropped_bomb;
    case 0xE1614C81:
        return EntityOverlayHandler::planted_bomb;
    case 0xE31DC1B8:
    case 0xC09C6702:
    case 0xA239EA8F:
    case 0x69629121:
        return EntityOverlayHandler::grenade_projectile;
    case 0xABE104C9:
        return EntityOverlayHandler::inferno;
    case 0x24E7D8E9:
    case 0x5AA1386D:
    case 0xAE060272:
    case 0xD3227436:
    case 0xDBDC655C:
        return EntityOverlayHandler::danger_zone_item;
    case 0xCD91F684:
        return EntityOverlayHandler::defuse_kit;
    default:
        return EntityOverlayHandler::none;
    }
}
std::vector<EntityOverlayHandler> entity_overlay_handlers(std::span<const std::uint32_t> hashes) {
    std::vector<EntityOverlayHandler> handlers(hashes.size());
    std::transform(hashes.begin(), hashes.end(), handlers.begin(), entity_overlay_handler);
    return handlers;
}
bool ammunition_weapon(std::uint16_t definition) noexcept {
    constexpr std::array<std::uint32_t, 3> bits{0xFF8B6F9Eu, 0xB00001DFu, 1};
    return definition < 65 && (bits[definition >> 5] & (std::uint32_t(1) << (definition & 31)));
}
EntityOverlayHandler select_world_entity(WorldEntityScan& result, const WorldEntity& entity,
                                         std::span<const EntityOverlayHandler> handlers,
                                         i32 maximum_players, i32 highest, std::uint32_t options,
                                         i32 kit_model, const Projection& projection,
                                         Vector3 view) {
    if (std::int32_t(entity.index) <= maximum_players || std::int32_t(entity.index) >= highest ||
        (entity.index & 0xFFFF0000u) || !entity.identity || entity.dormant)
        return EntityOverlayHandler::none;
    const auto handler = entity.class_index < handlers.size() ? handlers[entity.class_index]
                                                              : EntityOverlayHandler::none;
    if (handler != EntityOverlayHandler::none) {
        result.specialized.push_back({handler, &entity});
        return handler;
    }
    if (!options || entity.owner_handle != -1)
        return EntityOverlayHandler::none;
    std::uint32_t definition;
    if (entity.weapon) {
        definition = entity.definition;
        if (ammunition_weapon(entity.definition) &&
            (entity.ammunition < 0 || add(entity.ammunition, entity.reserve_ammunition) <= 0))
            return EntityOverlayHandler::none;
    } else {
        if (kit_model == -1 || std::int32_t(entity.model_index) != kit_model)
            return EntityOverlayHandler::none;
        definition = 55;
    }
    Point screen;
    if (!project_point(projection, entity.origin, screen) || definition >= 65)
        return EntityOverlayHandler::none;
    auto& selected = result.dropped[definition];
    const auto squared = distance_squared(view, entity.origin);
    if (selected.entity && squared >= selected.distance_squared)
        return EntityOverlayHandler::none;
    selected = {&entity, squared, screen};
    return EntityOverlayHandler::none;
}
WorldEntityScan scan_world_entities(std::span<const WorldEntity> entities,
                                    std::span<const EntityOverlayHandler> handlers,
                                    i32 maximum_players, i32 highest, std::uint32_t options,
                                    i32 kit_model, const Projection& projection, Vector3 view) {
    WorldEntityScan result;
    for (const auto& entity : entities)
        select_world_entity(result, entity, handlers, maximum_players, highest, options, kit_model,
                            projection, view);
    return result;
}
namespace {
template <class Options, class Color, class Ammunition, class Capacity>
void draw_dropped(ui::RenderQueue& q, const WorldEntityScan& scan, DroppedEntityProviders providers,
                  Options options, Color color, Ammunition ammunition_at, Capacity capacity_at) {
    if (!options())
        return;
    for (std::uint16_t definition = 0; definition != 65; ++definition) {
        const auto& dropped = scan.dropped[definition];
        if (!dropped.entity)
            continue;
        if (!q.fonts)
            throw std::logic_error("Dropped entity ESP requires a font backend");
        const auto font_height = ui::font_height(*q.fonts, 4);
        auto baseline = dropped.screen.y;
        Point measured{};
        if (options() & 1) {
            measured.x = 40;
            draw_weapon_icon(q, providers.icons, definition, {dropped.screen.x, baseline}, color(),
                             &measured.x);
            baseline = add(baseline, 12);
        }
        if (options() & 2) {
            draw_weapon_text(q, providers.names, definition,
                             {dropped.screen.x, add(baseline, font_height / 2)}, color(),
                             &measured);
            baseline = add(baseline, font_height);
        }
        if (definition != 55 && (options() & 8)) {
            if (!measured.x)
                measured.x = 20;
            const auto x = sub(dropped.screen.x, measured.x / 2);
            const auto ammunition = ammunition_at(*dropped.entity);
            if (ammunition != -1) {
                const auto capacity = capacity_at(*dropped.entity);
                if (capacity > 0) {
                    const auto fraction =
                        sse_min(sse_max(float(ammunition) / float(capacity), 0.0f), 1.0f);
                    const auto filled = truncate(float(measured.x) * fraction);
                    q.rectangle({sub(x, 1), add(baseline, 2), add(measured.x, 2), 4,
                                 (((color() >> 24) * 160) / 255) << 24});
                    q.rectangle({x, add(baseline, 3), filled, 2, color()});
                    if (fraction < 0.25f)
                        q.text(add(x, filled), add(baseline, 5), 0xC8FFFFFFu, 0x14, 0,
                               decimal(ammunition));
                }
            }
            baseline = add(baseline, 6);
        }
        if (options() & 16)
            distance_label(q, {dropped.screen.x, add(baseline, font_height / 2)}, color(),
                           dropped.distance_squared);
    }
}
}
void draw_dropped_entities(ui::RenderQueue& q, const WorldEntityScan& scan, std::uint32_t options,
                           std::uint32_t color, DroppedEntityProviders providers) {
    draw_dropped(
        q, scan, providers, [=] { return options; }, [=] { return color; },
        [](const WorldEntity& entity) { return entity.ammunition; },
        [=](const WorldEntity& entity) {
            if (!providers.ammunition_capacity)
                throw std::logic_error(
                    "Dropped entity ammo bar requires the engine capacity query");
            return providers.ammunition_capacity(providers.context, entity);
        });
}
void draw_dropped_entities(ui::RenderQueue& q, const WorldEntityScan& scan,
                           const WorldEspConfig& config, DroppedEntityProviders providers,
                           WorldEntityEngine& engine) {
    draw_dropped(
        q, scan, providers, [&] { return std::uint32_t(config.dropped_weapons.value); },
        [&] { return config.dropped_weapon_color.value; },
        [&](const WorldEntity& entity) { return engine.ammunition(entity); },
        [&](const WorldEntity& entity) { return engine.ammunition_capacity(entity); });
}
void draw_dropped_bomb(ui::RenderQueue& q, const Projection& projection, Vector3 view,
                       std::uint32_t options, std::uint32_t color, std::u16string_view label,
                       const DroppedBombState& bomb) {
    if (!options || (bomb.game_rules_present && !bomb.game_rules_allow) || bomb.bomb_state != 0)
        return;
    Point screen;
    if (!project_point(projection, bomb.origin, screen))
        return;
    q.text(screen.x, screen.y, color, 0x14, 0, label);
    if (options & 16)
        distance_label(q, {screen.x, add(screen.y, 10)}, color,
                       distance_squared(view, bomb.origin));
}
void draw_defuse_kit(ui::RenderQueue& q, const Projection& projection, Vector3 view,
                     std::uint32_t options, std::uint32_t color, i32 team, std::int16_t model,
                     i32 kit_model, Vector3 origin) {
    if (!options || team == 2 || std::int32_t(model) != kit_model)
        return;
    Point screen;
    if (!project_point(projection, origin, screen))
        return;
    q.text(screen.x, screen.y, color, 0x14, 0, u"KIT");
    if (options & 16)
        distance_label(q, {screen.x, add(screen.y, 10)}, color, distance_squared(view, origin));
}
void draw_hostage(ui::RenderQueue& q, const Projection& projection, Vector3 local, bool enabled,
                  bool box, bool distance, std::uint32_t color, const HostageState& entity) {
    if (!enabled || entity.dormant || (entity.effects & 2) || entity.life_state > 1 ||
        !entity.bones_available || !entity.model_available || entity.hitboxes.empty())
        return;
    i32 left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (const auto& hitbox : entity.hitboxes) {
        if (hitbox.bone >= entity.bones.size() || hitbox.bone >= 256)
            throw std::out_of_range("Hostage hitbox exceeds the native bone workspace");
        const auto first = transform(entity.bones[hitbox.bone], hitbox.minimum);
        const auto second = transform(entity.bones[hitbox.bone], hitbox.maximum, true);
        for (const auto corner : std::array<Vector3, 8>{{{first.x, first.y, first.z},
                                                         {first.x, first.y, second.z},
                                                         {first.x, second.y, first.z},
                                                         {first.x, second.y, second.z},
                                                         {second.x, first.y, first.z},
                                                         {second.x, first.y, second.z},
                                                         {second.x, second.y, first.z},
                                                         {second.x, second.y, second.z}}}) {
            Point screen;
            if (!project_point(projection, corner, screen))
                return;
            left = std::min(left, screen.x);
            right = std::max(right, screen.x);
            top = std::min(top, screen.y);
            bottom = std::max(bottom, screen.y);
        }
    }
    left = std::max(left, 0);
    right = std::max(right, 0);
    top = std::max(top, 0);
    bottom = std::max(bottom, 0);
    if (left >= right || top >= bottom)
        return;
    const auto width = sub(right, left), height = sub(bottom, top);
    const auto center = add(left, width / 2);
    q.text(center, sub(top, 5), color, 0x14, 0, u"HOSTAGE");
    auto baseline = add(top, 3);
    if (box) {
        q.rectangle_outline({sub(left, 1), sub(top, 1), add(width, 2), add(height, 2), 0x8C000000u},
                            1);
        q.rectangle_outline({left, top, width, height, color}, 1);
        q.rectangle_outline({add(left, 1), add(top, 1), sub(width, 2), sub(height, 2), 0x8C000000u},
                            1);
        baseline = add(bottom, 6);
    }
    if (distance)
        distance_label(q, {center, baseline}, 0xC8FFFFFFu, distance_squared(local, entity.origin));
}
void reset_danger_zone_items(std::span<DangerZoneItem> items) noexcept {
    for (auto& item : items)
        item.distance = std::numeric_limits<float>::max();
}
std::array<DangerZoneItemTemplate, 20> danger_zone_item_templates() noexcept {
    using C = WorldEspConfig;
    return {{{0x6427A53Fu, u"DRONE GUN", &C::drone_gun},
             {0x66F439F2u, u"BLACKHAWK", &C::blackhawk},
             {0xEC5261E8u, u"DRONE", &C::drone},
             {0x2D7C23AAu, u"RANDOM CASE", &C::random_case},
             {0x807E926Cu, u"TOOL CASE", &C::tool_case},
             {0xDE29550Au, u"PISTOL CASE", &C::pistol_case},
             {0x4DF8C90Cu, u"EXPLOSIVE CASE", &C::explosive_case},
             {0x6D711DFDu, u"HEAVY WEAPON CASE", &C::heavy_weapon_case},
             {0xF9D1C0FCu, u"LIGHT WEAPON CASE", &C::light_weapon_case},
             {0x31DBE954u, u"DUFFLEBAG", &C::dufflebag},
             {0xA22A50FFu, u"JAMMER", &C::jammer},
             {0x17927C7Au, u"AMMOBOX", &C::ammobox},
             {0x97187BC7u, u"ARMOR", &C::armor},
             {0xDEC16CE1u, u"ARMOR+HELMET", &C::armor},
             {0x7CAFC101u, u"HELMET", &C::armor},
             {0x87365B32u, u"PARACHUTE", &C::parachute_pack},
             {0x1D83CCB9u, u"BRIEFCASE", &C::briefcase},
             {0xDDE26198u, u"UPGRADE TABLET ZONE", &C::tablet_upgrade_zone},
             {0x1EB563B0u, u"UPGRADE TABLET DRONE", &C::tablet_upgrade_drone},
             {0xA44C6770u, u"CASH", &C::cash_stack}}};
}
void collect_danger_zone_item(std::span<DangerZoneItem> items, const WorldEntity& entity,
                              Vector3 local) noexcept {
    if (entity.dormant || entity.model_index < 0)
        return;
    for (auto& item : items) {
        if (item.model_index != entity.model_index)
            continue;
        if (!(item.setting ? item.setting->enabled.value : item.enabled))
            return;
        const auto distance = std::sqrt(distance_squared(local, entity.origin));
        if (distance >= item.distance)
            return;
        item.distance = distance;
        item.closest_origin = entity.origin;
        return;
    }
}
void draw_danger_zone_items(ui::RenderQueue& q, const Projection& projection, i32 game_mode,
                            bool enabled, std::span<const DangerZoneItem> items) {
    if (game_mode != 6 || !enabled)
        return;
    for (const auto& item : items) {
        if (item.distance == std::numeric_limits<float>::max())
            continue;
        Point screen;
        if (!project_point(projection, item.closest_origin, screen))
            continue;
        const auto color = item.setting ? item.setting->color.value : item.color;
        q.text(screen.x, screen.y, color, 0x14, 0, item.label);
        const auto distance_color =
            ((item.setting ? item.setting->color.value : item.color) & 0xFF000000u) | 0x00FFFFFFu;
        auto text = decimal(mul(sub(2, truncate(item.distance / -12.0f)) / 5, 5));
        text += u" FT";
        q.text(screen.x, add(screen.y, 9), distance_color, 0x14, 0, text);
    }
}
} // namespace skeet::esp
