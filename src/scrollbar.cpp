#include "skeet/gui_layout.hpp"
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
        throw std::overflow_error("Invalid scrollbar integer division");
    return a / b;
}
}
Rectangle scrollbar_thumb(const Scrollbar& scroll, i32 width, float scale) { // 0x433C8760
    if (!scroll.content_height)
        return {};
    const auto scaled_width = round_layout_coordinate(float(width) * scale);
    auto height = divide(mul(scroll.viewport.y, scroll.viewport.y), scroll.content_height);
    if (height < 20)
        height = 20;
    Rectangle thumb{sub(scroll.viewport.x, scaled_width), 2, sub(scaled_width, 2), height};
    const auto travel = sub(sub(scroll.viewport.y, height), 4);
    const auto extent = sub(scroll.content_height, scroll.viewport.y);
    if (travel >= 0 && extent > 0)
        thumb.y = add(divide(mul(scroll.position, travel), extent), 2);
    return thumb;
}
bool scrollbar_mouse(Scrollbar& scroll, std::uint32_t message, Point cursor,
                     float scale) { // 0x4340B88B
    if (message == 0x200) {
        if (!scroll.dragging)
            return false;
        const auto thumb = scrollbar_thumb(scroll, 14, scale);
        const auto y = sub(cursor.y, scroll.grab_offset);
        if (y < 1)
            scroll.position = 0;
        else if (add(add(thumb.height, 1), y) > scroll.viewport.y)
            scroll.position = sub(scroll.content_height, scroll.viewport.y);
        else if (scroll.viewport.y > thumb.height)
            scroll.position = divide(mul(sub(scroll.content_height, scroll.viewport.y), sub(y, 1)),
                                     sub(scroll.viewport.y, thumb.height));
        return true;
    }
    if (message == 0x201) {
        const auto thumb = scrollbar_thumb(scroll, 14, scale);
        if (contains(thumb, cursor)) {
            scroll.dragging = true;
            scroll.grab_offset = sub(cursor.y, thumb.y);
        } else {
            const auto width = round_layout_coordinate(scale * 14.0f);
            if (!contains(
                    {sub(sub(scroll.viewport.x, width), 1), 1, width, sub(scroll.viewport.y, 2)},
                    cursor))
                return false;
            const auto delta =
                cursor.y < thumb.y ? sub(1, scroll.viewport.y) : sub(scroll.viewport.y, 1);
            move_scroll_position(scroll.position, scroll.viewport.y, scroll.content_height, delta);
        }
        return true;
    }
    if (message == 0x202)
        scroll.dragging = false;
    return false;
}
} // namespace skeet::ui
