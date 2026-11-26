#include "skeet/menu/rage_weapon_selector.hpp"
#include "skeet/menu_tree.hpp"
#include "skeet/scrollbar_render.hpp"
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
i32 mul(i32 a, i32 b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) * std::uint32_t(b));
}
constexpr std::array<std::uint8_t, 12> symbols{0, 61, 19, 7, 27, 28, 38, 9, 40, 1, 64, 31};
std::uint8_t& group_flag(const std::array<std::uint8_t*, 12>& groups, std::uint32_t group) {
    if (group >= groups.size() || !groups[group])
        throw std::out_of_range("RAGE selector requires its weapon-group flag");
    return *groups[group];
}
}
RageWeaponSelector::RageWeaponSelector() {
    context_menu.parent = this;
    context_menu.selected = select_context;
}
RageWeaponSelector::RageWeaponSelector(Widget* owner, std::u16string_view label,
                                       std::uint32_t* selection, WidgetIdentities& identities,
                                       std::span<std::uint8_t> groups,
                                       RageWeaponSelectorActions callbacks) // 0x4348E18F
    : ComboBox(owner, label, selection, true, identities), actions(callbacks),
      context_menu(this, {u"Copy", u"Paste"}, identities) {
    bind_enabled_groups(groups);
    initialize_selector();
}
RageWeaponSelector::RageWeaponSelector(Widget* owner, std::u16string_view label,
                                       std::uint32_t* selection, WidgetIdentities& identities,
                                       std::span<BooleanConfig*, 12> groups,
                                       RageWeaponSelectorActions callbacks)
    : ComboBox(owner, label, selection, true, identities), actions(callbacks),
      context_menu(this, {u"Copy", u"Paste"}, identities) {
    bind_enabled_groups(groups);
    initialize_selector();
}
void RageWeaponSelector::initialize_selector() {
    context_menu.selected = select_context;
    geometry.bounds = {0, 0, 214, 46};
    geometry.default_size = {214, 46};
    geometry.horizontal_offset = 0;
    default_row_height = row_height = 30;
    set_options({{u"Global", 0},
                 {u"G3SG1 / SCAR-20", 6},
                 {u"SSG 08", 8},
                 {u"AWP", 7},
                 {u"R8 Revolver", 10},
                 {u"Desert Eagle", 9},
                 {u"Pistol", 1},
                 {u"Zeus", 11},
                 {u"Rifle", 3},
                 {u"Shotgun", 4},
                 {u"SMG", 2},
                 {u"Machine gun", 5}});
}
void RageWeaponSelector::bind_enabled_groups(std::span<std::uint8_t> values) {
    if (values.size() < enabled_groups.size())
        throw std::invalid_argument("RAGE selector requires twelve weapon-group flags");
    for (std::size_t i = 0; i < enabled_groups.size(); ++i)
        enabled_groups[i] = &values[i];
}
void RageWeaponSelector::bind_enabled_groups(std::span<BooleanConfig*, 12> descriptors) {
    for (std::size_t i = 0; i < enabled_groups.size(); ++i) {
        if (!descriptors[i])
            throw std::invalid_argument("RAGE selector requires a live BooleanConfig descriptor");
        enabled_groups[i] = &descriptors[i]->value;
    }
}
bool RageWeaponSelector::hit_test(Point cursor) const { // 0x433FBC33
    return context_menu.hit_test(cursor) || ComboBox::hit_test(cursor);
}
void RageWeaponSelector::reset_input() { // 0x433D1EB9
    ComboBox::reset_input();
    context_menu.reset_input();
}
void RageWeaponSelector::value_changed() { // 0x4332A7C8
    ComboBox::value_changed();
    if (!actions.switch_group)
        throw std::logic_error("RAGE selector group store is not installed");
    actions.switch_group(actions.context, false);
}
void RageWeaponSelector::resize(const SliderMetrics&) { // 0x43381FC8
    if (!parent)
        throw std::logic_error("RAGE selector layout requires its category");
    geometry.bounds.width = parent->geometry.bounds.width;
    geometry.bounds.height = parent->geometry.bounds.height;
    update_scroll();
}
void RageWeaponSelector::dpi_changed(float factor, const SliderMetrics& metrics) { // 0x433F2A28
    ComboBox::dpi_changed(factor, metrics);
    if (!parent)
        throw std::logic_error("RAGE selector layout requires its category");
    geometry.bounds.width = parent->geometry.bounds.width;
    geometry.bounds.height = parent->geometry.bounds.height;
    icon_size = scale_size({76, 20}, factor);
    global_icon_size = round_layout_coordinate(factor * 32.0f);
    checkbox_size = round_layout_coordinate(factor * 8.0f);
    context_menu.dpi_changed(factor, metrics);
}
void RageWeaponSelector::on_layout() { // 0x43330C8A
    geometry.bounds.x = geometry.horizontal_offset;
    geometry.bounds.y = 0;
}
void RageWeaponSelector::interact_options(const InputMessage& message, Point cursor) { // 0x4337FEBF
    const auto y = sub(sub(cursor.y, geometry.bounds.height), geometry.bounds.y);
    if (y < 0)
        hovered_item = -1;
    else {
        const auto offset = scroll.enabled ? sub(scroll.position, scroll.position % row_height) : 0;
        const auto index = std::uint32_t(add(offset, y) / row_height);
        if (index < options.size()) {
            hovered_item = y / row_height;
            if (held) {
                const auto group = options[index].value;
                if (cursor.x < round_layout_coordinate(scale * 50.0f)) {
                    if (message.message != 0x202)
                        return;
                    if (group)
                        group_flag(enabled_groups, group) = group_flag(enabled_groups, group) == 0;
                    held = false;
                    return;
                }
                if ((value ? *value : 0) != group)
                    set_value(group);
            }
        }
    }
    if (message.message == 0x202)
        open = held = false;
}
bool RageWeaponSelector::combo_mouse(const InputMessage& message, Point cursor) {
    // 0x4340C49A dispatches its option interaction through virtual slot 0x34.
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
        const auto wheel = std::bit_cast<std::int16_t>(std::uint16_t(message.parameter >> 16));
        move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height,
                             mul(row_height, wheel > 0 ? -4 : 4));
        return true;
    }
    return false;
}
bool RageWeaponSelector::mouse(const InputMessage& message, Point cursor) { // 0x433EA5E2
    bool handled = false;
    bool blocked = false;
    if (context_menu.open) {
        if (context_menu.mouse(message, cursor))
            return true;
        blocked = context_menu.open && context_menu.hit_test(cursor);
    }
    if (!blocked) {
        handled = combo_mouse(message, cursor);
        if (handled) {
            if (message.message != 0x200)
                context_menu.reset_input();
            if (message.message == 0x205) {
                const auto y = sub(cursor.y, add(geometry.bounds.height, geometry.bounds.y));
                bool valid = false;
                if (y < 0) {
                    if (ComboBox::hit_test(cursor) && !options.empty()) {
                        context_group = value ? *value : 0;
                        valid = true;
                    }
                } else {
                    const auto offset =
                        scroll.enabled ? sub(scroll.position, scroll.position % row_height) : 0;
                    const auto index = std::uint32_t(add(offset, y) / row_height);
                    if (index < options.size()) {
                        context_group = options[index].value;
                        valid = true;
                    }
                }
                if (valid) {
                    context_menu.geometry.bounds.x =
                        add(context_menu.geometry.horizontal_offset, cursor.x);
                    context_menu.geometry.bounds.y = cursor.y;
                    context_menu.open = true;
                }
            }
        }
    }
    auto* category = owning_category(this);
    if (!category)
        throw std::logic_error("RAGE selector input requires its category");
    category->background = hovered || open ? 0xff191919 : 0xff171717;
    return handled;
}
void RageWeaponSelector::select_context(Widget& owner, PopupMenu&, std::uint32_t index) {
    static_cast<RageWeaponSelector&>(owner).context_selected(index);
}
void RageWeaponSelector::context_selected(std::uint32_t index) { // 0x433E3B88 / 0x433E7F67
    if (context_group >= 12)
        return;
    if (index == 0) {
        if (!actions.copy_group)
            throw std::logic_error("RAGE selector clipboard encoder is not installed");
        actions.copy_group(actions.context, context_group);
    } else if (index == 1) {
        if (!actions.paste_group)
            throw std::logic_error("RAGE selector clipboard decoder is not installed");
        if (actions.paste_group(actions.context, context_group) &&
            context_group == (value ? *value : 0)) {
            if (!actions.switch_group)
                throw std::logic_error("RAGE selector group store is not installed");
            actions.switch_group(actions.context, true);
        }
    }
}
void RageWeaponSelector::draw_checkbox(RenderQueue& queue, Point p, std::uint32_t group, i32 height,
                                       std::uint32_t accent) const { // 0x433D24EC
    const auto color = group_flag(enabled_groups, group) ? accent : 0xbb646464;
    const auto y = add(p.y, sub(height, checkbox_size) / 2);
    queue.rectangle_outline({p.x, y, checkbox_size, checkbox_size, 0xff0c0c0c}, 1);
    queue.vertical_gradient({add(p.x, 1), add(y, 1), sub(checkbox_size, 2), sub(checkbox_size, 2),
                             color, subtract_color(color, 0x00282828)});
}
void RageWeaponSelector::draw_option(RenderQueue& queue, Point p, i32 height, std::uint32_t index,
                                     std::uint32_t flags,
                                     std::uint32_t accent) const { // 0x4332C152
    if (index >= options.size())
        throw std::out_of_range("RAGE selector option is outside its vector");
    const auto group = options[index].value;
    if (group != UINT32_MAX) {
        if (group >= symbols.size())
            throw std::out_of_range("RAGE selector icon group is invalid");
        if (!icons.load)
            throw std::logic_error("RAGE selector icon cache is not installed");
        RageSelectorIcon icon;
        const auto requested = group ? icon_size.y : global_icon_size;
        if (icons.load(icons.context, queue, symbols[group], {requested, requested}, icon)) {
            const auto tint = flags & 1 || index == selected_item ? 0xc8ffffffU : 0x50ffffffU;
            const auto x = add(p.x, sub(sub(geometry.bounds.width, icon.size.x),
                                        round_layout_coordinate(scale * 40.0f)));
            const auto y = icon.size.y < height ? add(p.y, sub(height, icon.size.y) / 2) : p.y;
            queue.texture(
                {x, y, std::uint32_t(icon.texture), tint, 0, 0, icon.size.x, icon.size.y, 0, 1.0f});
        }
    }
    const Point checkbox{add(p.x, round_layout_coordinate(scale * 13.0f)), p.y};
    draw_checkbox(queue, checkbox, group, height, accent);
    const Point text{add(checkbox.x, round_layout_coordinate(scale * 20.0f)), sub(p.y, 1)};
    ComboBox::draw_option(queue, text, height, index, 0xc8ffffff,
                          index == selected_item ? flags | 1 : flags);
}
void RageWeaponSelector::draw_combo(RenderQueue& queue, Point origin, std::uint32_t accent) const {
    Point position{add(origin.x, geometry.bounds.x), add(origin.y, geometry.bounds.y)};
    draw_label(queue, position, sub(sub(parent->geometry.bounds.width, geometry.bounds.x), 8));
    const auto text_color = enabled ? 0x88ffffffU : 0x32ffffffU;
    const auto height = !name.empty() && !inline_element
                            ? sub(geometry.bounds.height, interaction_offset)
                            : geometry.bounds.height;
    draw_option(queue, {add(position.x, round_layout_coordinate(scale * 7.0f)), position.y}, height,
                selected_item, 0, accent);
    draw_down_arrow(
        queue,
        {add(sub(position.x, round_layout_coordinate(scale * 12.0f)), geometry.bounds.width),
         add(position.y, sub(height, 4) / 2)},
        text_color);
    if (!enabled || !open)
        return;
    const auto bounds_reset = queue.is_bounds_reset();
    const auto depth = queue.get_depth();
    queue.reset_bounds(false);
    queue.set_depth(0.15f);
    const Point popup{add(origin.x, geometry.bounds.x),
                      add(add(origin.y, geometry.bounds.y), add(geometry.bounds.height, 1))};
    const auto rows = std::uint32_t(std::min<std::size_t>(8, options.size()));
    queue.outlined_rectangle(
        {popup.x, popup.y, geometry.bounds.width, mul(row_height, i32(rows)), 0xff232323},
        0xff0c0c0c, 1);
    const auto offset = scroll.enabled ? scroll.position / row_height : 0;
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto index = row + std::uint32_t(offset);
        if (index >= options.size())
            break;
        const auto y = add(popup.y, mul(row_height, i32(row)));
        const auto highlight = row == std::uint32_t(hovered_item);
        if (highlight)
            queue.rectangle({add(popup.x, 1), add(y, 1), sub(geometry.bounds.width, 2),
                             sub(row_height, 2), 0xff191919});
        draw_option(queue, {add(popup.x, round_layout_coordinate(scale * 7.0f)), y}, row_height,
                    index, highlight || index == selected_item ? 1 : 0, accent);
    }
    if (scroll.enabled)
        draw_scrollbar(queue, scroll, popup, scale);
    queue.reset_bounds(bounds_reset);
    queue.set_depth(depth);
}
void RageWeaponSelector::draw(RenderQueue& queue, Point origin,
                              std::uint32_t accent) const { // 0x4336F7D5
    draw_combo(queue, origin, accent);
    context_menu.draw(queue, origin, accent);
}
}
