#include "skeet/font_cache.hpp"

namespace skeet::ui {
FontSet& select_fonts(FontBackend& backend, std::uint32_t flags) noexcept { // 0x434172EC
    return (flags & 0x100) || backend.scale == 1.0f ? backend.default_fonts : backend.scaled_fonts;
}
void prepare_glyphs(FontBackend* backend, std::u16string_view text,
                    std::uint32_t flags) { // 0x43342F16
    if (!backend)
        return;
    auto& fonts = select_fonts(*backend, flags);
    const auto index = (flags & 2) ? 2 : (flags & 1) ? 1 : (flags & 4) ? 4 : 0;
    for (const auto character : text) {
        for (auto* link = &fonts[index]; link; link = link->next)
            if (link->cache && link->cache->prepare(link->cache->context, character))
                break;
    }
}
} // namespace skeet::ui
