#include "skeet/esp/flag_table_initializer.hpp"
#include "detail.hpp"
#include "flag_table_detail.hpp"

namespace skeet::esp {
using namespace detail;

FlagEntry detail::initial_flag_entry(std::size_t index) noexcept { //43485097 values
    switch (index) {
    case 0:
        return {1, 0xC8FFFFFFu, u"H"};
    case 1:
        return {2, 0xC8FFFFFFu, u"K"};
    case 2:
        return {4, 0xC8FFFFFFu, u"HK"};
    case 3:
        return {8, 0xD2E1B43Cu, u"ZOOM"};
    case 4:
        return {0x10, 0xD2E1B43Cu, u"BLIND"};
    case 5:
        return {0x20, 0xD2E1B43Cu, u"RELOAD"};
    case 6:
        return {0x40, 0xFF3200FFu, u"C4"};
    case 7:
        return {0x80, 0xFF3200FFu, u"VIP"};
    case 8:
        return {0x100, 0xFF3200FFu, u"DEFUSE"};
    case 9:
        return {0x200, 0xC8FFFFFFu, u"FD"};
    case 10:
        return {0x400, 0xFF3200FFu, u"PIN"};
    case 11:
        return {0x800, 0xC8FFFFFFu, u"HIT"};
    case 12:
        return {0x1000, 0xC8FFFFFFu, u"O"};
    case 13:
        return {0x2000, 0xC8FFFFFFu, u"X"};
    case 14:
        return {0x20000, 0xFF3200FFu, u"X"};
    default:
        return {0, 0, u""};
    }
}
std::array<FlagEntry, 18> default_flag_entries() noexcept {
    std::array<FlagEntry, 18> table;
    for (std::size_t index = 0; index < table.size(); ++index)
        table[index] = detail::initial_flag_entry(index);
    return table;
}

float reload_cycle(const AnimationLayer& layer) noexcept { // 0x433CEFB3
    if (layer.count >= 1 && layer.weight != 0.0f && layer.activity == 0x3C7)
        return layer.cycle;
    return 1.0f;
}

bool correction_pending(const RecordHistory& h) noexcept { // 0x4334D33C
    if (!h.correction_count)
        return false;
    const auto& slot = h.corrections[h.correction_index % 6];
    return slot.counter < sub(slot.limit, 1);
}

bool fake_duck(const RecordHistory& h, float amount, float speed) noexcept { // 0x433D1FFD
    return h.count >= 2 && sub(h.ticks[h.first & 31], h.ticks[(h.first + 1) & 31]) > 2 &&
           amount > 0.0f && amount < 1.0f && speed == 8.0f;
}

std::uint32_t active_flags(const PlayerFlagState& p) noexcept { // 0x43417468
    std::uint32_t flags = 0;
    if (p.game_mode != 2) {
        flags = (p.helmet ? 1u : 0u) | (p.armor > 0 ? 2u : 0u);
        if (flags == 3)
            flags = 4;
    }
    if (!p.dormant) {
        if (p.scoped)
            flags |= 8;
        if (p.flash_alpha > 20.0f)
            flags |= 0x10;
        if (p.vip)
            flags |= 0x80;
        if (p.team == 3 && p.defusing)
            flags |= 0x100;
        if (fake_duck(p.history, p.duck_amount, p.duck_speed))
            flags |= 0x200;
        if (p.weapon_valid) {
            if (p.ammunition != -1 && reload_cycle(p.animation) != 1.0f)
                flags |= 0x20;
            if (p.weapon_kind == 9 && p.pin_pulled)
                flags |= 0x400;
        }
    }
    if (p.entity_index == p.bomb_owner_index)
        flags |= 0x40;
    // The engine time vcall returns through x87; the original rounds the
    // extended quotient to float only after adding 0.5.
    const auto now = truncate(
        float(static_cast<long double>(p.engine_time) / static_cast<long double>(p.tick_interval) +
              0.5L));
    const auto difference = sub(p.last_hit_tick, now);
    const auto magnitude = difference < 0 ? sub(0, difference) : difference;
    if (magnitude <= 16)
        flags |= 0x800;
    if (p.on_shot_enabled) {
        if (p.target_is_player && p.local_player_present && !p.local_player_disallowed &&
            p.on_shot_extra_enabled)
            flags |= 0x1000;
        if (p.history.count) {
            const auto tick = truncate(p.simulation_time / p.simulation_tick_interval + 0.5f);
            if (tick < p.history.ticks[p.history.first & 31])
                flags |= 0x20000;
            else if (correction_pending(p.history))
                flags |= 0x2000;
        }
    }
    return flags;
}

void draw_flags(ui::RenderQueue& q, Point& cursor, i32 height, std::uint32_t mask,
                std::span<const FlagEntry, 18> table) { // 0x4340A7AA..0x4340A818
    if (!mask)
        return;
    for (const auto& entry : table) {
        if (!(entry.mask & mask))
            continue;
        q.text(cursor.x, cursor.y, entry.color, 4, 0, entry.text);
        cursor.y = add(cursor.y, height);
    }
}

void draw_flags(ui::RenderQueue& q, Point& cursor, i32 height, std::uint32_t mask,
                const FlagTable& table) { //4340A7AA..4340A818
    if (!mask)
        return;
    for (std::size_t index = 0; index < 18; ++index) {
        const auto& row = table.rows()[index];
        if (!(row.mask & mask))
            continue;
        q.text(cursor.x, cursor.y, row.color, 4, 0, table.text(index));
        cursor.y = add(cursor.y, height);
    }
}

} // namespace skeet::esp
