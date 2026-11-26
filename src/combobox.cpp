#include "skeet/combobox.hpp"
#include "skeet/font_metrics.hpp"
#include <algorithm>
#include <bit>
#include <limits>
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
i32 mul(i32 a, i32 b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) * std::uint32_t(b));
}
i32 divide(i32 a, i32 b) {
    if (!b || (a == std::numeric_limits<i32>::min() && b == -1))
        throw std::overflow_error("Invalid combobox integer division");
    return a / b;
}
}

ComboBox::ComboBox() {
    kind = 4;
}
ComboBox::ComboBox(Widget* owner, std::u16string_view label, std::uint32_t* setting, bool same_line,
                   WidgetIdentities& identities) // 0x43399927
    : Widget(owner, label, 4, setting != nullptr, identities), value(setting) {
    inline_element = same_line;
    geometry.default_size = {158, !name.empty() && !same_line ? 35 : 20};
    geometry.bounds.width = geometry.default_size.x;
    geometry.bounds.height = geometry.default_size.y;
    update_scroll();
}
void ComboBox::set_options(std::vector<ComboBoxOption> replacement) { // 0x433EE01C
    options = std::move(replacement);
    const auto current = value ? *value : 0;
    for (std::size_t i = 0; i < options.size(); ++i)
        if (options[i].value == current)
            selected_item = static_cast<std::uint32_t>(i);
    update_scroll();
}
ComboBox::ComboBox(Widget* owner, std::u16string_view label, std::int32_t* setting, bool same_line,
                   WidgetIdentities& identities)
    // C++ permits access through the corresponding signed/unsigned type.
    // Preserve the setting's original 32-bit storage rather than copying it.
    : ComboBox(owner, label, reinterpret_cast<std::uint32_t*>(setting), same_line, identities) {}
void ComboBox::set_value(std::uint32_t requested) { // 0x433F49B3
    if ((value ? *value : 0) != requested) {
        if (value)
            *value = requested;
        value_changed();
    }
}
void ComboBox::value_changed() { // 0x433ADEE6
    const auto current = value ? *value : 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].value == current) {
            selected_item = static_cast<std::uint32_t>(i);
            break;
        }
    }
    notify_callbacks();
}
void ComboBox::config_loaded() { // 0x43383770
    if (!value)
        return;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].value == *value) {
            selected_item = static_cast<std::uint32_t>(i);
            return;
        }
    }
    if (!options.empty())
        set_value(options.front().value);
}
void ComboBox::reset_input() { // 0x433FA887
    hovered_item = -1;
    hovered = false;
    open = held = false;
    scroll.dragging = false;
}
i32 ComboBox::input_priority() const {
    return open ? 2 : 0;
} // 0x4338A0B0
bool ComboBox::hit_test(Point cursor) const { // 0x43388257
    auto bounds = geometry.bounds;
    if (open)
        bounds.height = add(
            bounds.height,
            add(1, mul(static_cast<i32>(std::min<std::size_t>(8, options.size())), row_height)));
    if (!name.empty() && !inline_element) {
        bounds.height = sub(bounds.height, interaction_offset);
        bounds.y = add(bounds.y, interaction_offset);
    }
    return contains(bounds, cursor);
}
void ComboBox::interact_options(const InputMessage& message, Point cursor) { // 0x43325388
    const auto y = sub(sub(cursor.y, geometry.bounds.height), geometry.bounds.y);
    if (y < 0)
        hovered_item = -1;
    else {
        const auto offset =
            scroll.enabled ? mul(divide(scroll.position, row_height), row_height) : 0;
        const auto index = static_cast<std::uint32_t>(divide(add(offset, y), row_height));
        if (index < options.size()) {
            hovered_item = divide(y, row_height);
            if (held && (value ? *value : 0) != options[index].value)
                set_value(options[index].value);
        }
    }
    if (message.message == 0x202)
        open = held = false;
}
bool ComboBox::mouse(const InputMessage& message, Point cursor) { // 0x4340C49A
    if (!enabled)
        return false;
    if (open && scroll.enabled &&
        scrollbar_mouse(
            scroll, message.message,
            {sub(cursor.x, geometry.bounds.x), sub(sub(cursor.y, row_height), geometry.bounds.y)},
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
            interact_options(message, cursor);
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
                interact_options(message, cursor);
            return true;
        }
        held = false;
        return false;
    }
    if (message.message == 0x204 || message.message == 0x205)
        return hit_test(cursor);
    if (message.message == 0x20a && open && scroll.enabled && hit_test(cursor)) {
        const auto wheel =
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(message.parameter >> 16));
        move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height,
                             mul(row_height, wheel > 0 ? -4 : 4));
        return true;
    }
    return false;
}
void ComboBox::update_scroll() noexcept { // 0x433B09E2
    if (options.size() < 9) {
        scroll.enabled = false;
        return;
    }
    scroll.enabled = true;
    scroll.viewport = {geometry.bounds.width, mul(row_height, 8)};
    scroll.content_height = mul(static_cast<i32>(options.size()), row_height);
    move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height, 0);
}
void ComboBox::resize(const SliderMetrics& metrics) { // 0x4332BB67
    const auto width = sub(parent->geometry.bounds.width, metrics.category_padding);
    geometry.bounds.width = width < metrics.minimum_width
                                ? metrics.minimum_width
                                : std::min(width, metrics.preferred_maximum_width);
    update_scroll();
}
void ComboBox::dpi_changed(float factor, const SliderMetrics&) { // 0x43367728
    scale = factor;
    resize_element(geometry, factor);
    row_height = round_layout_coordinate(float(default_row_height) * factor);
    interaction_offset = round_layout_coordinate(float(default_interaction_offset) * factor);
    update_scroll();
}
void ComboBox::draw_box(RenderQueue& queue, Point position) const { // 0x43327D7A
    const bool highlight = hovered || open;
    const auto first = highlight ? 0xff292929U : enabled ? 0xff1f1f1fU : 0x501f1f1fU;
    const auto second = highlight ? 0xff2e2e2eU : enabled ? 0xff242424U : 0x50242424U;
    queue.vertical_gradient({add(position.x, 1), add(position.y, 1), sub(geometry.bounds.width, 2),
                             sub(row_height, 2), first, second});
    queue.rectangle_outline({position.x, position.y, geometry.bounds.width, row_height, 0xff0c0c0c},
                            1);
}
void ComboBox::draw_option(RenderQueue& queue, Point position, i32 height, std::uint32_t index,
                           std::uint32_t text_color, std::uint32_t flags) const { // 0x433B1508
    if (index >= options.size() || options[index].name.empty())
        return;
    if (!queue.fonts)
        throw std::logic_error("Combobox text requires its native font backend");
    const auto text_height = font_height(*queue.fonts, flags & 1);
    queue.text(position.x, add(position.y, divide(sub(height, text_height), 2)), text_color, flags,
               sub(geometry.bounds.width, round_layout_coordinate(scale * 20.0f)),
               options[index].name);
}
void ComboBox::draw(RenderQueue& queue, Point origin, std::uint32_t accent) const { // 0x433B8D0F
    Point position{add(origin.x, geometry.bounds.x), add(origin.y, geometry.bounds.y)};
    draw_label(queue, position, sub(sub(parent->geometry.bounds.width, geometry.bounds.x), 8));
    const auto text_color = enabled ? 0x88ffffffU : 0x32ffffffU;
    draw_box(queue, position);
    const auto height = !name.empty() && !inline_element
                            ? sub(geometry.bounds.height, interaction_offset)
                            : geometry.bounds.height;
    draw_option(queue, {add(position.x, round_layout_coordinate(scale * 7.0f)), position.y}, height,
                selected_item, text_color, 0);
    const auto arrow_x =
        add(sub(position.x, round_layout_coordinate(scale * 12.0f)), geometry.bounds.width);
    const auto arrow_y = add(position.y, divide(sub(height, 4), 2));
    queue.rectangle({arrow_x, sub(arrow_y, 1), 5, 1, 0xff000000});
    queue.rectangle({arrow_x, arrow_y, 5, 1, text_color});
    queue.rectangle({add(arrow_x, 1), add(arrow_y, 1), 3, 1, text_color});
    queue.rectangle({add(arrow_x, 2), add(arrow_y, 2), 1, 1, text_color});
    if (!enabled || !open)
        return;
    const auto bounds_reset = queue.is_bounds_reset();
    const auto depth = queue.get_depth();
    queue.reset_bounds(false);
    queue.set_depth(0.15f);
    const Point popup{add(origin.x, geometry.bounds.x),
                      add(add(origin.y, geometry.bounds.y), add(geometry.bounds.height, 1))};
    const auto rows = static_cast<std::uint32_t>(std::min<std::size_t>(8, options.size()));
    queue.outlined_rectangle({popup.x, popup.y, geometry.bounds.width,
                              mul(row_height, static_cast<i32>(rows)), 0xff232323},
                             0xff0c0c0c, 1);
    const auto offset = scroll.enabled ? divide(scroll.position, row_height) : 0;
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto index = row + static_cast<std::uint32_t>(offset);
        if (index >= options.size())
            break;
        const auto row_y = add(popup.y, mul(row_height, static_cast<i32>(row)));
        const auto highlight = row == static_cast<std::uint32_t>(hovered_item);
        if (highlight)
            queue.rectangle({add(popup.x, 1), add(row_y, 1), sub(geometry.bounds.width, 2),
                             sub(row_height, 2), 0xff191919});
        draw_option(queue, {add(popup.x, round_layout_coordinate(scale * 7.0f)), row_y}, row_height,
                    index, index == selected_item ? accent : 0xc8ffffffU,
                    highlight || index == selected_item ? 1 : 0);
    }
    if (scroll.enabled) { // 0x433E7AFE
        const auto width = round_layout_coordinate(scale * 7.0f);
        queue.rectangle({sub(add(popup.x, scroll.viewport.x), add(width, 1)), add(popup.y, 1),
                         width, sub(scroll.viewport.y, 2), 0xff282828});
        const auto thumb = scrollbar_thumb(scroll, 7, scale);
        queue.rectangle(
            {add(popup.x, thumb.x), add(popup.y, thumb.y), thumb.width, thumb.height, 0xff414141});
    }
    queue.reset_bounds(bounds_reset);
    queue.set_depth(depth);
}
}
