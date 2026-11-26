#include "skeet/render/backend.hpp"
#include "skeet/render/font_runtime.hpp"
#include "skeet/core.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace skeet::ui {
namespace {
std::int32_t add_text(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t subtract_text(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::uint32_t text_color(std::uint32_t value) noexcept {
    return (value & 0xff00ff00u) | ((value & 255) << 16) | ((value >> 16) & 255);
}
FontLink* image_font(FontLink& root, char16_t c, GlyphImage& out) {
    for (auto* link = &root; link; link = link->next) {
        if (!link->cache)
            continue;
        if (!link->cache->runtime)
            throw std::logic_error("Text drawing requires a concrete font runtime");
        if (link->cache->runtime->glyph(c, out))
            return link;
    }
    return nullptr;
}
enum class TextTokenKind : std::uint8_t { glyph, newline, color };
struct ResolvedTextToken {
    TextTokenKind kind;
    IDirect3DBaseTexture9* texture;
    std::uint32_t argb;
    std::int32_t advance_first;
    std::int32_t advance_second;
    float width;
    float height;
    float u0;
    float v0;
    float u1;
    float v1;
    bool drawable;
};
std::uint32_t parse_escape_argb(std::u16string_view content, std::size_t index) noexcept {
    std::uint32_t rgba = 0;
    for (std::size_t j = 0; j < 8; ++j) {
        const auto ch = content[index + 1 + j];
        std::uint32_t nibble;
        if (ch >= u'0' && ch <= u'9')
            nibble = std::uint32_t(ch - u'0');
        else if (ch >= u'a' && ch <= u'f')
            nibble = std::uint32_t(ch - u'a' + 10);
        else if (ch >= u'A' && ch <= u'F')
            nibble = std::uint32_t(ch - u'A' + 10);
        else {
            char digits[9]{};
            for (std::size_t k = 0; k < 8; ++k)
                digits[k] = static_cast<char>(content[index + 1 + k]);
            rgba = static_cast<std::uint32_t>(std::strtoul(digits, nullptr, 16));
            return (rgba >> 8) | (rgba << 24);
        }
        rgba = (rgba << 4) | nibble;
    }
    return (rgba >> 8) | (rgba << 24);
}
}

const RendererBackend::FontTexture* RendererBackend::font_texture(const FontLink& link) const noexcept {
    const auto at = std::find_if(font_textures_.begin(), font_textures_.end(),
                                 [&](const auto& record) { return record.link == &link; });
    return at == font_textures_.end() || !at->texture ? nullptr : &*at;
}
void RendererBackend::upload_fonts() { // 433C96CF..433C973A / 4334A887
    if (!fonts)
        return;
    auto upload = [&](FontSet& set) {
        for (auto& root : set) {
            if (!root.cache)
                continue;
            auto* font = root.cache->runtime;
            if (!font)
                throw std::logic_error("Font uploading requires a concrete font runtime");
            if (!font->consume_dirty())
                continue;
            auto at = std::find_if(font_textures_.begin(), font_textures_.end(),
                                   [&](const auto& record) { return record.link == &root; });
            if (at == font_textures_.end()) {
                font_textures_.push_back({&root, nullptr});
                at = font_textures_.end() - 1;
            }
            const auto atlas_read = font->lock_atlas();
            const auto atlas = atlas_read.view();
            IDirect3DTexture9* uploaded = nullptr;
            if (SUCCEEDED(upload_texture(atlas.width, atlas.height, std::as_bytes(atlas.pixels),
                                         &uploaded))) {
                if (at->texture)
                    at->texture->Release();
                at->texture = uploaded;
                at->width = atlas.width;
                at->height = atlas.height;
            } else {
                font->mark_dirty();
            }
        }
    };
    upload(fonts->default_fonts);
    if (fonts->scale != 1.0f)
        upload(fonts->scaled_fonts);
}

void RendererBackend::text(std::int32_t x, std::int32_t y, std::uint32_t color, std::uint32_t flags,
                           std::int32_t maximum_width,
                           std::u16string_view content) { // 4339D0B8 / 4334173D
    if (!fonts)
        return;
    auto& roots = select_fonts(*fonts, flags);
    const auto index = (flags & 2) ? 2 : (flags & 1) ? 1 : (flags & 4) ? 4 : 0;
    auto& root = roots[index];
    if (!root.cache)
        return;
    if (!root.cache->runtime)
        throw std::logic_error("Text drawing requires a concrete font runtime");
    if ((flags & 0x104) == 4 && fonts->scale != 1.0f)
        flags |= 8;
    if (content.empty())
        return;

    const auto line_height = root.cache->runtime->height();
    std::array<ResolvedTextToken, 256> inline_tokens;
    std::vector<ResolvedTextToken> heap_tokens;
    std::size_t token_count = 0;
    const bool use_heap = content.size() > inline_tokens.size();
    if (use_heap)
        heap_tokens.reserve(content.size());
    auto push_token = [&](const ResolvedTextToken& token) {
        if (use_heap)
            heap_tokens.push_back(token);
        else
            inline_tokens[token_count++] = token;
    };

    Point size{0, line_height};
    std::int32_t measure_width = 0;
    const FontLink* cached_font = nullptr;
    const FontTexture* cached_texture = nullptr;
    for (std::size_t i = 0; i < content.size(); ++i) {
        const auto character = content[i];
        if (character == u'\n') {
            measure_width = 0;
            size.y = add_text(size.y, line_height);
            push_token({TextTokenKind::newline, nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        false});
        } else if (character == u'\a' && content.size() - i > 8) {
            push_token({TextTokenKind::color, nullptr, parse_escape_argb(content, i), 0, 0, 0.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.0f, false});
            i += 8;
        } else {
            GlyphImage glyph;
            auto* font = image_font(root, character, glyph);
            if (!font)
                continue;
            measure_width =
                add_text(measure_width, add_text(glyph.advance_first, glyph.advance_second));
            if (measure_width > size.x)
                size.x = measure_width;
            if (font != cached_font) {
                cached_font = font;
                cached_texture = font_texture(*font);
            }
            const bool drawable = cached_texture && glyph.right <= float(cached_texture->width) &&
                                  glyph.bottom <= float(cached_texture->height);
            if (!drawable) {
                push_token({TextTokenKind::glyph, nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            false});
                continue;
            }
            const auto width = float(cached_texture->width), height = float(cached_texture->height);
            push_token({TextTokenKind::glyph, cached_texture->texture, 0, glyph.advance_first,
                        glyph.advance_second, glyph.right - glyph.left, glyph.bottom - glyph.top,
                        glyph.left / width, glyph.top / height, glyph.right / width,
                        glyph.bottom / height, true});
        }
    }
    const std::span<const ResolvedTextToken> tokens =
        use_heap ? std::span<const ResolvedTextToken>(heap_tokens)
                 : std::span<const ResolvedTextToken>(inline_tokens.data(), token_count);

    if (flags & 0x90) {
        if (maximum_width && size.x > maximum_width)
            size.x = maximum_width;
        if (flags & 0x10) {
            x = subtract_text(x, size.x / 2);
            y = subtract_text(y, size.y / 2);
        } else if (flags & 0x80)
            x = subtract_text(x, size.x);
    }
    auto pass = [&](std::int32_t origin_x, std::int32_t origin_y, std::uint32_t argb,
                    bool shadow) { // 4332667C
        auto pen_x = origin_x, pen_y = origin_y;
        const auto limit = maximum_width ? add_text(origin_x, maximum_width)
                                         : std::numeric_limits<std::int32_t>::max();
        for (const auto& token : tokens) {
            if (token.kind == TextTokenKind::newline) {
                pen_x = origin_x;
                pen_y = add_text(pen_y, line_height);
            } else if (token.kind == TextTokenKind::color) {
                if (!shadow)
                    argb = token.argb;
            } else if (token.drawable) {
                const auto draw_x = add_text(pen_x, token.advance_first);
                const auto next = add_text(draw_x, token.advance_second);
                if (next >= limit)
                    return;
                const auto left = float(draw_x), top = float(pen_y);
                const auto right = left + token.width;
                const auto bottom = top + token.height;
                if (quad(token.texture, left, top, right, bottom, {argb, argb, argb, argb}, token.u0,
                         token.v0, token.u1, token.v1))
                    pen_x = next;
            }
        }
    };
    if (flags & 8) {
        const auto shadow_alpha = std::min(color >> 24, 128u);
        pass(add_text(x, 1), add_text(y, 1), (shadow_alpha << 24) | 0x010101u, true);
    } else if (flags & 0x20) {
        const auto outline_color = (color & 0xff000000u) | 0x0c0c0cu;
        constexpr std::array<Point, 4> offsets{{{1, 1}, {-1, -1}, {-1, 1}, {1, -1}}};
        for (const auto offset : offsets)
            pass(add_text(x, offset.x), add_text(y, offset.y), outline_color, true);
    }
    pass(x, y, text_color(color), false);
}
} // namespace skeet::ui
