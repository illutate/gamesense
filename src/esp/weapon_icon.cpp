#include "skeet/esp/weapon_icon.hpp"
#include "detail.hpp"

namespace skeet::esp {
using namespace detail;

void draw_weapon_icon(ui::RenderQueue& q, WeaponIconProvider provider, std::uint16_t definition,
                      Point position, std::uint32_t color, i32* output) {
    std::uint16_t normalized;
    if (std::uint16_t(definition - 500) <= 25)
        normalized = 42;
    else {
        if (std::uint16_t(definition - 1) > 63)
            return;
        normalized = definition;
    }
    WeaponIcon icon;
    if (!provider.load(provider.context, q, normalized, definition, 25, icon) || icon.texture == -1)
        return;
    const auto scale = 12.0f / float(icon.height);
    const auto height = truncate(float(icon.height) * scale);
    const auto width = truncate(float(icon.width) * scale);
    if (output)
        *output = width;
    const auto x = sub(position.x, width / 2);
    const auto alpha = std::min<std::uint32_t>(color >> 24, 180) << 24;
    q.texture({x, position.y, std::uint32_t(icon.texture), alpha, 0, 0, width, height, 1, 1.0f});
    q.texture({sub(x, 1), sub(position.y, 1), std::uint32_t(icon.texture), color, 0, 0, width,
               height, 1, 1.0f});
}

} // namespace skeet::esp
