#include "skeet/text_metrics.hpp"
#include <bit>
#include <stdexcept>

namespace skeet::ui {
namespace {
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
}
Point measure_text(RenderQueue& queue, std::u16string_view content,
                   std::uint32_t flags) { // 0x4341825C / 0x4332B578 / 0x4338AA26
    prepare_glyphs(queue.fonts, content, flags);
    if (!queue.fonts)
        throw std::logic_error("Text measurement requires a font backend");
    auto& fonts = select_fonts(*queue.fonts, flags);
    const auto index = (flags & 2) ? 2 : (flags & 1) ? 1 : (flags & 4) ? 4 : 0; // 0x4332AF73
    auto& root = fonts[index];
    std::int32_t height = 0;
    if (root.cache) {
        if (!root.cache->height)
            throw std::logic_error("Font provider has no height callback");
        height = root.cache->height(root.cache->context);
    }
    Point size{0, height};
    std::int32_t width = 0;
    for (std::size_t i = 0; i < content.size(); ++i) {
        const auto character = content[i];
        if (character == u'\n') {
            width = 0;
            size.y = add(size.y, height);
        } else if (character == u'\a' && content.size() - i > 8) {
            i += 8;
        } else {
            for (auto* link = &root; link; link = link->next) {
                if (!link->cache)
                    continue;
                if (!link->cache->metrics)
                    throw std::logic_error("Font provider has no glyph metrics callback");
                GlyphMetrics metrics;
                if (link->cache->metrics(link->cache->context, character, metrics)) {
                    width = add(width, add(metrics.advance_first, metrics.advance_second));
                    if (width > size.x)
                        size.x = width;
                    break;
                }
            }
        }
    }
    return size;
}
}
