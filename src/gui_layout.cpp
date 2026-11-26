#include "skeet/gui_layout.hpp"
#include <bit>
#include <immintrin.h>
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
i32 truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
}

i32 round_layout_coordinate(float value) noexcept { // 0x433CC0D6
    // Preserve CVTSS2SI: MXCSR rounding mode and INT_MIN for invalid conversions.
    return _mm_cvtss_si32(_mm_set_ss(value));
}
Point scale_size(Point size, float scale) noexcept { // 0x43328177
    const auto height = round_layout_coordinate(float(size.y) * scale);
    return {round_layout_coordinate(float(size.x) * scale), height};
}
void resize_element(ElementGeometry& element, float scale) noexcept { // 0x4336EDA0
    const auto size = scale_size(element.default_size, scale);
    element.bounds.width = size.x;
    element.bounds.height = size.y;
    element.label_height = round_layout_coordinate(scale * 14.0f);
    if (element.horizontal_offset != 0)
        element.horizontal_offset = round_layout_coordinate(scale * 20.0f);
}
void move_scroll_position(i32& position, i32 viewport_height, i32 content_height,
                          i32 delta) noexcept { // 0x433E1D26
    auto updated = add(position, delta);
    const auto maximum = sub(content_height, viewport_height);
    if (updated < 0)
        updated = 0;
    else if (maximum < updated)
        updated = maximum;
    position = updated;
}

void layout_category_grid(const MenuGeometry& menu, const TabGeometry& tab) { // 0x43384477
    const auto left = add(add(tab.width, tab.padding), menu.start_padding.x);
    const auto top = add(menu.start_padding.y, tab.padding);
    const auto right = sub(menu.size.x, add(menu.end_padding.x, tab.padding));
    const auto bottom = sub(menu.size.y, add(menu.end_padding.y, tab.padding));
    std::array<i32, 24> reserved_height{};
    for (auto* category : tab.categories) {
        auto& item = *category;
        if (item.column < 0 || item.column >= 24 || item.column_span < 0 || item.column_span > 24)
            throw std::out_of_range("Category grid indices exceed the native 24-column storage");
        auto& bounds = item.element.bounds;
        auto& defaults = item.element.default_size;
        const auto available_top = add(reserved_height[item.column], top);
        const auto width = float(sub(right, left));
        const auto height = float(sub(bottom, available_top));
        if (item.row_span == -1) {
            bounds.height = defaults.y = add(item.fixed_height, mul(item.margin, 2));
            // The native upper bound is column_span, not column + column_span.
            for (auto column = item.column; column < item.column_span; ++column)
                reserved_height[column] = bounds.height;
        } else {
            bounds.height = defaults.y = truncate((float(item.row_span) / 24.0f) * height);
        }
        bounds.width = defaults.x = truncate((float(item.column_span) / 24.0f) * width);
        bounds.x = add(truncate(float(item.column) * (width / 24.0f) + float(left)),
                       item.element.horizontal_offset);
        bounds.y = truncate((height / 24.0f) * float(item.row) + float(available_top));
        bounds.x = add(bounds.x, item.margin);
        bounds.y = add(bounds.y, item.margin);
        bounds.width = sub(bounds.width, mul(item.margin, 2));
        bounds.height = sub(bounds.height, mul(item.margin, 2));
        const auto final_right = sub(right, item.margin);
        const auto final_bottom = sub(bottom, item.margin);
        if (item.column_span + item.column >= 24 && add(bounds.width, bounds.x) != final_right)
            bounds.width = defaults.x = sub(final_right, bounds.x);
        if (item.row_span + item.row >= 24 && add(bounds.height, bounds.y) != final_bottom)
            bounds.height = defaults.y = sub(final_bottom, bounds.y);
    }
}

void reflow_category(CategoryLayout& category) { // 0x433AD23F
    const auto padding = category.padding;
    const auto initial_height = category.bounds.height;
    auto next_row = padding;
    auto inline_row = padding;
    const auto initial_elements = category.elements;
    for (auto* element : initial_elements) {
        if (!element->visible)
            continue;
        auto& geometry = element->geometry;
        auto& bounds = geometry.bounds;
        if (!element->inline_element) {
            bounds.x = add(geometry.horizontal_offset, padding);
            bounds.y = next_row;
            if (bounds.height > 0) {
                inline_row = next_row;
                next_row = add(add(next_row, 5), bounds.height);
            }
        } else if (element->kind == 12 || element->kind == 9) {
            bounds.x =
                sub(add(sub(geometry.horizontal_offset, bounds.width), category.bounds.width),
                    mul(category.padding, 2));
            bounds.y = add(inline_row, 1);
            inline_row = add(add(inline_row, 5), bounds.height);
        } else {
            bounds.x = add(geometry.horizontal_offset, padding);
            bounds.y = inline_row;
        }
        element->layout(*element);
    }
    auto& scroll = category.scroll;
    if (category.elements.size() < 2 || next_row <= sub(initial_height, 5)) {
        scroll.position = 0;
        scroll.enabled = false;
    } else {
        scroll.enabled = true;
        scroll.viewport = {category.bounds.width, category.bounds.height};
        scroll.content_height = add(category.padding, next_row);
        move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height, 0);
    }
    const auto offset = scroll.enabled ? scroll.position : 0;
    const auto visible_bottom = add(category.bounds.height, offset);
    const auto final_elements = category.elements;
    for (auto* element : final_elements) {
        const auto bottom = add(element->geometry.bounds.height, element->geometry.bounds.y);
        if (bottom < offset || visible_bottom < bottom)
            element->reset(*element);
    }
}

void update_listbox_scroll(ListboxLayout& listbox) noexcept { // 0x4333FF11
    if (listbox.visible_rows >= listbox.item_count) {
        listbox.scroll.enabled = false;
        return;
    }
    auto& scroll = listbox.scroll;
    scroll.enabled = true;
    scroll.viewport.x = listbox.element.bounds.width;
    scroll.viewport.y = listbox.search_visible
                            ? sub(listbox.element.bounds.height, listbox.row_height)
                            : listbox.element.bounds.height;
    scroll.content_height = mul(add(listbox.item_count, 1), listbox.row_height);
    move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height, 0);
}
void reflow_listbox(ListboxLayout& listbox, float scale) { // 0x4339E4DF
    resize_element(listbox.element, scale);
    const auto row_height = round_layout_coordinate(scale * 18.0f);
    if (row_height == 0 || (row_height == -1 && listbox.element.bounds.height == INT32_MIN))
        throw std::domain_error("Listbox row height would cause an x86 division fault");
    const auto rows = listbox.element.bounds.height / row_height;
    listbox.row_height = row_height;
    listbox.visible_rows = listbox.search_visible ? sub(rows, 1) : rows;
    update_listbox_scroll(listbox);
}

} // namespace skeet::ui
