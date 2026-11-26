#include "skeet/popup_menu.hpp"
#include "skeet/font_metrics.hpp"
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
i32 mul(i32 a, std::uint32_t b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) * b);
}
}

PopupMenu::PopupMenu() {
    kind = 14;
    inline_element = true;
    geometry.horizontal_offset = 0;
}
PopupMenu::PopupMenu(Widget* owner, std::vector<std::u16string> labels,
                     WidgetIdentities& identities) // 0x4339D578
    : Widget(owner, u"", 14, true, identities), options(std::move(labels)) {
    inline_element = true;
    geometry.horizontal_offset = 0;
    geometry.default_size = {100, mul(20, std::uint32_t(options.size()))};
    geometry.bounds = {-100, 0, geometry.default_size.x, geometry.default_size.y};
}
bool PopupMenu::hit_test(Point cursor) const { // 0x4332FAE6
    if (!open)
        return false;
    if (!parent)
        throw std::logic_error("Popup hit testing requires an owning widget");
    const auto& p = parent->geometry.bounds;
    auto bounds = geometry.bounds;
    bounds.x = add(bounds.x, p.x);
    bounds.y = add(bounds.y, p.y);
    return contains(bounds, cursor);
}
void PopupMenu::set_selected(std::uint32_t index) noexcept { // 0x433C859C
    if (index != 0xffffffffU && options.size() < index)
        index = std::uint32_t(options.size());
    selected_item = index;
}
void PopupMenu::select_at(Point cursor, bool commit) { // 0x4338798F
    if (!parent)
        throw std::logic_error("Popup selection requires an owning widget");
    const auto offset = sub(sub(cursor.y, parent->geometry.bounds.y), geometry.bounds.y);
    if (offset < 0) {
        hovered_item = -1;
        return;
    }
    if (!row_height)
        throw std::domain_error("Popup row height is zero");
    const auto index = std::uint32_t(offset / row_height);
    if (index < options.size()) {
        hovered_item = std::bit_cast<i32>(index);
        if (commit) {
            if (!selected)
                throw std::logic_error("Popup selection callback is not installed");
            selected(*parent, *this, index);
        }
    }
}
bool PopupMenu::mouse(const InputMessage& message, Point cursor) { // 0x433AFC02
    if (!open)
        return false;
    if (message.message == 0x200) {
        hovered = hit_test(cursor);
        if (hovered) {
            select_at(cursor, false);
            return true;
        }
        hovered_item = -1;
    } else if (message.message == 0x201) {
        if (hit_test(cursor)) {
            held = true;
            return true;
        }
    } else if (message.message == 0x202 && held) {
        if (hit_test(cursor)) {
            select_at(cursor, true);
            open = false;
            held = false;
            return true;
        }
        held = false;
    }
    return false;
}
void PopupMenu::reset_input() { // 0x433CCE79
    hovered_item = -1;
    hovered = false;
    open = false;
    held = false;
}
void PopupMenu::dpi_changed(float scale, const SliderMetrics&) { // 0x433E8BC8
    row_height = round_layout_coordinate(scale * 20.0f);
    geometry.bounds.width = round_layout_coordinate(scale * 100.0f);
    geometry.bounds.height =
        add(round_layout_coordinate(scale * 0.0f), mul(row_height, std::uint32_t(options.size())));
    geometry.label_height = round_layout_coordinate(scale * 14.0f);
    if (geometry.horizontal_offset != 0)
        geometry.horizontal_offset = round_layout_coordinate(scale * 20.0f);
    geometry.bounds.x = sub(geometry.horizontal_offset, geometry.bounds.width);
    geometry.bounds.y = 0;
}
void PopupMenu::draw(RenderQueue& queue, Point origin, std::uint32_t accent) const { // 0x43330648
    if (!enabled || !open)
        return;
    if (!queue.fonts)
        throw std::logic_error("Popup rendering requires a font backend");
    const auto previous_bounds_reset = queue.is_bounds_reset();
    const auto previous_depth = queue.get_depth();
    queue.reset_bounds(false);
    queue.set_depth(0.15f);
    const auto x = add(origin.x, geometry.bounds.x), y = add(origin.y, geometry.bounds.y);
    queue.outlined_rectangle({x, y, geometry.bounds.width, geometry.bounds.height, 0xff232323U},
                             0xff0c0c0cU, 1);
    const auto text_offset = sub(row_height, font_height(*queue.fonts, 0)) / 2;
    for (std::uint32_t index = 0; index < options.size(); ++index) {
        const auto row_y = add(y, mul(row_height, index));
        const bool highlighted = index == std::uint32_t(hovered_item);
        if (highlighted)
            queue.rectangle({add(x, 1), add(row_y, 1), sub(geometry.bounds.width, 2),
                             sub(row_height, 2), 0xff191919U});
        queue.text(add(x, 10), add(row_y, text_offset),
                   index == selected_item ? accent : 0xc8ffffffU, std::uint32_t(highlighted),
                   geometry.bounds.width, options[index]);
    }
    queue.reset_bounds(previous_bounds_reset);
    queue.set_depth(previous_depth);
}

} // namespace skeet::ui
