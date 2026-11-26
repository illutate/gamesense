#include "skeet/esp/indicators.hpp"
#include "skeet/render/font_runtime.hpp"
#include "skeet/render/backend.hpp"
#include "skeet/text_metrics.hpp"
#include "detail.hpp"
#include <array>
#include <limits>
#include <stdexcept>
#define NOMINMAX
#include <windows.h>

namespace skeet::esp {
using namespace detail;
namespace {
i32 rounded(float value) noexcept {
    return _mm_cvtss_si32(_mm_set_ss(value));
}
}

void draw_indicator(ui::RenderQueue& queue, i32& cursor, std::string_view utf8, std::uint32_t color,
                    const IndicatorLayout& layout, i32 texture, float fraction) {
    if (utf8.empty())
        return;
    if (utf8.size() > std::size_t(std::numeric_limits<int>::max()))
        throw std::length_error("Indicator UTF-8 input exceeds the native length field");
    std::array<wchar_t, 8193> wide{};
    const auto converted = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                               static_cast<int>(utf8.size()), wide.data(), 8192);
    if (converted <= 0)
        return;
    const auto length = std::min(converted, 8191);
    wide[length] = 0;
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::u16string_view text{reinterpret_cast<const char16_t*>(wide.data()),
                                   std::size_t(length)};
    const auto padding_y = rounded(layout.scale * 2.0f);
    const auto spacing = rounded(layout.scale * 4.0f);
    const auto edge = rounded(layout.scale * 25.0f);
    const auto size = ui::measure_text(queue, text, 10);
    Point texture_size{};
    if (texture != -1) {
        if (layout.texture_source)
            layout.texture_source->texture_size(std::uint32_t(texture), &texture_size.x,
                                                &texture_size.y);
        else if (std::uint32_t(texture) < layout.textures.size())
            texture_size = layout.textures[std::uint32_t(texture)];
        if (texture_size.y > size.y) {
            texture_size.x =
                truncate(float(texture_size.x) / float(texture_size.y) * float(size.y) + 0.5f);
            texture_size.y = size.y;
        }
    }
    const bool circle = fraction >= 0.0f;
    i32 diameter = size.x;
    if (circle) {
        if (!queue.fonts)
            throw std::logic_error("Indicator circle requires a font backend");
        diameter =
            sub(ui::font_ascent(*queue.fonts, 2), ui::font_internal_leading(*queue.fonts, 2));
    }
    const auto row_height = add(size.y, mul(padding_y, 2));
    //4340063D queries this queue's screen after text/texture/font metrics;
    //43400644 consumes its height before subtracting the live row cursor.
    const auto screen_height = queue.screen().y;
    const auto y = sub(screen_height, cursor);
    auto texture_x = size.x;
    auto texture_y = size.y;
    auto text_x = edge;
    if (texture != -1) {
        texture_x = add(edge, spacing);
        texture_y = add(y, sub(row_height, texture_size.y) / 2);
        text_x = add(add(texture_size.x, texture_x), spacing);
    }
    text_x = add(text_x, spacing);
    const auto text_y = add(y, sub(row_height, size.y) / 2);
    auto width = add(add(text_x, size.x), spacing);
    i32 circle_x = texture_size.x;
    i32 circle_y = texture_size.y;
    if (circle) {
        circle_x = add(add(width, spacing), diameter / 2);
        circle_y = add(add(y, sub(row_height, diameter) / 2), diameter / 2);
        width = add(add(width, diameter), mul(spacing, 2));
    }
    width = add(width, edge);
    if (color >= 0x01000000u && utf8.front() != '\n') {
        const auto half = width / 2;
        queue.horizontal_gradient({0, y, half, row_height, 0, 0x33000000u});
        queue.horizontal_gradient({half, y, sub(width, half), row_height, 0x33000000u, 0});
        if (texture != -1)
            queue.texture({texture_x, texture_y, std::uint32_t(texture), color, 0, 0,
                           texture_size.x, texture_size.y, 0, 1.0f});
        queue.text(text_x, text_y, color, 10, 0, text);
        if (circle) {
            const auto radius = float(diameter / 2);
            const auto inner = rounded(layout.scale * 3.0f);
            const auto outer = add(inner, rounded(layout.scale * 2.0f));
            queue.circle_outline(
                {circle_x, circle_y, 0xC8000000u, radius + 1.0f, 0.0f, 1.0f, float(outer)});
            if (fraction > 0.0f)
                queue.circle_outline(
                    {circle_x, circle_y, 0xC8FFFFFFu, radius, 0.0f, fraction, float(inner)});
        }
    }
    cursor = add(cursor, add(row_height, rounded(layout.scale * 8.0f)));
}

} // namespace skeet::esp
