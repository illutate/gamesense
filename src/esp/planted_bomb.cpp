#include "skeet/esp/planted_bomb.hpp"
#include "detail.hpp"
#include <cstdio>
#include <cstring>

namespace skeet::esp {
using namespace detail;
namespace {
float site_distance(Vector3 origin, Vector3 site) noexcept {
    const float x = origin.x - site.x, y = origin.y - site.y, z = origin.z - site.z;
    return (y * y + x * x) + z * z;
}
}

void draw_planted_bomb(ui::RenderQueue& queue, const WorldEspConfig& config,
                       PlantedBombOwner& owner, std::int32_t& cursor, const PlantedBombInput& input,
                       const IndicatorLayout& layout) {
    draw_planted_bomb(queue, config,
                      {reinterpret_cast<std::byte*>(&owner.transient), owner.peak_damage}, cursor,
                      input, layout);
}
void draw_planted_bomb(ui::RenderQueue& queue, const WorldEspConfig& config,
                       PlantedBombBinding owner, std::int32_t& cursor,
                       const PlantedBombInput& input, const IndicatorLayout& layout) {
    std::memset(owner.transient_word, 0, 4);
    if (!config.bomb.enabled.value || !input.bomb.ticking)
        return;
    auto indicator = [&](std::string_view text, std::uint32_t color, i32 texture) {
        if (input.scripts)
            draw_indicator(*input.scripts, queue, cursor, text, color, layout, texture, -1.0f);
        else
            draw_indicator(queue, cursor, text, color, layout, texture, -1.0f);
    };
    const auto& bomb = input.bomb;
    const auto remaining = bomb.detonation_time - input.current_time;
    if (remaining > 0.0f) {
        if (bomb.defuse_end > 0.0f && bomb.defuser_state == 1) {
            const bool finishes_in_time = !(bomb.defuse_end > bomb.detonation_time);
            const auto height = truncate(
                (float(sub(layout.screen_height, 2)) * (bomb.defuse_end - input.current_time)) /
                10.0f);
            queue.rectangle({0, 0, 20, layout.screen_height, 0x73000000u});
            const auto color =
                (finishes_in_time ? 0x7350FF00u : 0x73503200u) | (finishes_in_time ? 0x32u : 0xFFu);
            queue.rectangle({1, sub(sub(layout.screen_height, height), 2), 18, height, color});
        }
        char site = 'A';
        if (input.sites && site_distance(bomb.origin, input.sites->first) >
                               site_distance(bomb.origin, input.sites->second))
            site = 'B';
        char label[48]{};
        const auto length =
            std::snprintf(label, sizeof(label), "%c - %.1fs", site, double(remaining));
        if (length > 0)
            indicator({label, std::size_t(length)}, 0xC8FFFFFFu, input.label_texture);
    }
    if (!(remaining > -1.0f) || input.player.life_state != 0)
        return;
    if (input.player.health(input.player.context) <= 0)
        return;
    const auto damage = truncate(bomb_damage(input.registered_bomb, input.player.damage_target,
                                             owner.peak_damage, input.trace));
    if (damage == 0)
        return;
    if (damage >= input.player.health(input.player.context)) {
        indicator("FATAL", 0xFF3200FFu, -1);
    } else {
        char label[32]{};
        const auto length = std::snprintf(label, sizeof(label), "-%d HP", damage);
        if (length > 0)
            indicator({label, std::size_t(length)}, 0xFF69F3FCu, -1);
    }
}

} // namespace skeet::esp
