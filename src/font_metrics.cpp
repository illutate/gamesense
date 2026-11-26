#include "skeet/font_metrics.hpp"
#include <stdexcept>

namespace skeet::ui {
std::int32_t font_height(FontBackend& backend, std::uint32_t index) { // 0x4334113A / 0x4336C370
    auto& fonts = backend.scale == 1.0f ? backend.default_fonts : backend.scaled_fonts;
    if (index >= fonts.size())
        throw std::out_of_range("Font metric index exceeds native font roots");
    const auto* cache = fonts[index].cache;
    if (!cache)
        return 0;
    if (!cache->height)
        throw std::logic_error("Font provider has no height callback");
    return cache->height(cache->context);
}
}
