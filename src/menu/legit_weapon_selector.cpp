#include "skeet/menu/legit_menu.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace skeet::ui {
namespace {
using i32 = std::int32_t;
i32 add(i32 a, i32 b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) + std::uint32_t(b));
}
i32 sub(i32 a, i32 b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) - std::uint32_t(b));
}
}
LegitWeaponSelector::LegitWeaponSelector(Widget* owner, std::int32_t* selection,
                                         WidgetIdentities& identities) // 0x43484CFD
    : ComboBox(owner, u"Weapon type", selection, true, identities) {
    open = true;
    geometry.horizontal_offset = 0;
    geometry.bounds.x = 10;
    geometry.bounds.y = 0;
    geometry.bounds.height = geometry.default_size.y = 60;
    set_options({{u"Pistol", 1},
                 {u"SMG", 2},
                 {u"Rifle", 3},
                 {u"Shotgun", 4},
                 {u"Machine gun", 6},
                 {u"Sniper", 5}});
}
void LegitWeaponSelector::reset_input() { // 0x433F4BA9
    hovered_item = -1;
    hovered = held = false;
    open = true;
    scroll.dragging = false;
}
bool LegitWeaponSelector::hit_test(Point cursor) const { // 0x4334A278
    auto* category = owning_category(const_cast<LegitWeaponSelector*>(this));
    if (!category)
        throw std::logic_error("Weapon selector requires its native owning category");
    return geometry.bounds.x <= cursor.x &&
           cursor.x <= add(category->geometry.bounds.width, geometry.bounds.x) &&
           geometry.bounds.y <= cursor.y &&
           cursor.y <= add(category->geometry.bounds.height, geometry.bounds.y);
}
void LegitWeaponSelector::interact_weapons(const InputMessage& message,
                                           Point cursor) { // 0x4339DF7F
    hovered_item = -1;
    const auto relative_x = sub(cursor.x, geometry.bounds.x);
    if (sub(cursor.y, geometry.bounds.y) < 0 || relative_x < 0)
        return;
    if (cell_width == 0 || options.empty())
        throw std::logic_error("Weapon selector requires native nonempty options and cell width");
    const auto index =
        std::min(std::uint32_t(relative_x / cell_width), std::uint32_t(options.size() - 1));
    hovered_item = std::int32_t(index);
    if (held && (value ? *value : 0) != options[index].value)
        set_value(options[index].value);
    if (message.message == 0x202)
        open = held = false;
}
bool LegitWeaponSelector::mouse(const InputMessage& message,
                                Point cursor) { // 0x433E8C81 -> 0x4340C49A
    const auto result = [&]() {
        if (!enabled)
            return false;
        if (open && scroll.enabled &&
            scrollbar_mouse(scroll, message.message,
                            {sub(cursor.x, geometry.bounds.x),
                             sub(sub(cursor.y, row_height), geometry.bounds.y)},
                            scale))
            return true;
        if (message.message == 0x200) {
            if (!hit_test(cursor)) {
                hovered_item = -1;
                hovered = held = false;
                return false;
            }
            hovered = true;
            if (open)
                interact_weapons(message, cursor);
            return held;
        }
        if (message.message == 0x201) {
            if (!hit_test(cursor))
                return false;
            held = true;
            return true;
        }
        if (message.message == 0x202) {
            scroll.dragging = false;
            if (!held)
                return false;
            if (hit_test(cursor)) {
                if (!open) {
                    open = true;
                    held = false;
                } else
                    interact_weapons(message, cursor);
                return true;
            }
            held = false;
            return false;
        }
        if (message.message == 0x204 || message.message == 0x205)
            return hit_test(cursor);
        if (message.message == 0x20A && open && scroll.enabled && hit_test(cursor)) {
            const auto wheel = std::bit_cast<std::int16_t>(std::uint16_t(message.parameter >> 16));
            move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height,
                                 row_height * (wheel > 0 ? -4 : 4));
            return true;
        }
        return false;
    }();
    open = true;
    return result;
}
void LegitWeaponSelector::resize(const SliderMetrics&) { // 0x433B2F44
    auto* category = owning_category(this);
    if (!category)
        throw std::logic_error("Weapon selector resize requires its owning category");
    geometry.bounds.width = category->geometry.bounds.width;
    geometry.bounds.height = category->geometry.bounds.height;
}
void LegitWeaponSelector::dpi_changed(float factor, const SliderMetrics& metrics) { // 0x4336E814
    scale = factor;
    resize(metrics);
    cell_width = round_layout_coordinate(76.0f * factor);
    icon_height = round_layout_coordinate(20.0f * factor);
}
void LegitWeaponSelector::on_layout() { // 0x4338AAFF
    geometry.bounds.x = add(round_layout_coordinate(scale * 10.0f), geometry.horizontal_offset);
    geometry.bounds.y = 0;
}
void LegitWeaponSelector::draw(RenderQueue& queue, Point origin,
                               std::uint32_t) const { // 0x4336F5CA
    if (!icons.load)
        throw std::logic_error("Weapon selector drawing requires its native icon cache provider");
    constexpr std::uint16_t definitions[]{61, 19, 7, 27, 28, 40};
    auto x = add(origin.x, geometry.bounds.x);
    const auto y = add(origin.y, geometry.bounds.y);
    for (std::size_t i = 0; i != options.size(); ++i) {
        esp::WeaponIcon icon;
        if (i >= std::size(definitions))
            throw std::logic_error("Weapon selector options exceed the native icon table");
        if (icons.load(icons.context, queue, definitions[i], definitions[i], icon_height, icon)) {
            const auto color = i == selected_item                 ? 0xC8FFFFFFu
                               : i == std::uint32_t(hovered_item) ? 0x78FFFFFFu
                                                                  : 0x50FFFFFFu;
            auto icon_x = x;
            if (icon.width < cell_width)
                icon_x = add(icon_x, sub(cell_width, icon.width) / 2);
            auto icon_y = y;
            if (icon.height < geometry.bounds.height)
                icon_y = add(icon_y, sub(geometry.bounds.height, icon.height) / 2);
            queue.texture({icon_x, icon_y, std::uint32_t(icon.texture), color, 0, 0, icon.width,
                           icon.height, 0, 1.0f});
        }
        x = add(x, cell_width);
    }
}
} // namespace skeet::ui
