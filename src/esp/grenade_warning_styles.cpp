#include "skeet/esp/grenade_warning_styles.hpp"
#include "skeet/color_picker.hpp"
#include "skeet/render_queue.hpp"
#include "skeet/text_metrics.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace skeet::esp {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::uint32_t abgr_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept {
    return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) |
           std::uint32_t(r);
}

std::uint32_t scale_alpha(std::uint32_t abgr, float factor) noexcept {
    const float base_a = static_cast<float>(abgr >> 24);
    const auto a = static_cast<std::uint8_t>(std::clamp(base_a * factor, 0.0f, 255.0f));
    return (abgr & 0x00FFFFFFu) | (std::uint32_t(a) << 24);
}

std::uint32_t lerp_abgr(std::uint32_t a, std::uint32_t b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto ch = [&](int shift) -> std::uint32_t {
        const float va = static_cast<float>((a >> shift) & 0xFFu);
        const float vb = static_cast<float>((b >> shift) & 0xFFu);
        return static_cast<std::uint32_t>(std::clamp(va + (vb - va) * t, 0.0f, 255.0f)) << shift;
    };
    return ch(0) | ch(8) | ch(16) | ch(24);
}

std::u16string format_decimal_u16(std::int32_t value) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%d", value);
    std::u16string out;
    for (const char* p = buf; *p; ++p)
        out.push_back(static_cast<char16_t>(*p));
    return out;
}

std::u16string format_timer_u16(float seconds, bool space = false) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), space ? "%.1f s" : "%.1fs", double(std::max(0.0f, seconds)));
    std::u16string out;
    for (const char* p = buf; *p; ++p)
        out.push_back(static_cast<char16_t>(*p));
    return out;
}

struct ColorVertex {
    float x, y;
    std::uint32_t color;
};

void emit_gaussian_glow(ui::RenderQueue& queue, float cx, float cy, float radius,
                        std::uint32_t peak_abgr) {
    if (radius <= 1.0f || (peak_abgr >> 24) == 0)
        return;
    constexpr std::size_t segments = 24;
    constexpr std::array<float, 5> radii{0.0f, 0.25f, 0.52f, 0.78f, 1.0f};
    const auto ring_color = [&](float r_norm) -> std::uint32_t {
        if (r_norm >= 0.999f)
            return peak_abgr & 0x00FFFFFFu;
        const float w = std::exp(-3.4f * r_norm * r_norm) * (1.0f - r_norm * r_norm * r_norm * r_norm);
        return scale_alpha(peak_abgr, w);
    };
    constexpr std::size_t vert_count = segments * 3 + segments * 3 * 6;
    std::array<ColorVertex, vert_count> verts{};
    std::size_t pos = 0;
    for (std::size_t ring = 0; ring + 1 < radii.size(); ++ring) {
        const float r0 = radius * radii[ring];
        const float r1 = radius * radii[ring + 1];
        const std::uint32_t c0 = ring_color(radii[ring]);
        const std::uint32_t c1 = ring_color(radii[ring + 1]);
        for (std::size_t i = 0; i < segments; ++i) {
            const float a0 = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
            const float a1 =
                (2.0f * kPi * static_cast<float>(i + 1)) / static_cast<float>(segments);
            const float cos0 = std::cos(a0), sin0 = std::sin(a0);
            const float cos1 = std::cos(a1), sin1 = std::sin(a1);
            if (ring == 0) {
                verts[pos++] = {cx, cy, c0};
                verts[pos++] = {cx + cos0 * r1, cy + sin0 * r1, c1};
                verts[pos++] = {cx + cos1 * r1, cy + sin1 * r1, c1};
            } else {
                const ColorVertex p00{cx + cos0 * r0, cy + sin0 * r0, c0};
                const ColorVertex p01{cx + cos1 * r0, cy + sin1 * r0, c0};
                const ColorVertex p10{cx + cos0 * r1, cy + sin0 * r1, c1};
                const ColorVertex p11{cx + cos1 * r1, cy + sin1 * r1, c1};
                verts[pos++] = p00;
                verts[pos++] = p10;
                verts[pos++] = p11;
                verts[pos++] = p00;
                verts[pos++] = p11;
                verts[pos++] = p01;
            }
        }
    }
    const auto count = static_cast<std::uint32_t>(pos);
    auto* payload = queue.append(17, count * 12 + 20);
    const std::uint32_t primitive = 2; // TRIANGLELIST
    std::memcpy(payload, &primitive, 4);
    std::memcpy(payload + 4, &count, 4);
    std::memcpy(payload + 8, verts.data(), pos * sizeof(ColorVertex));
}

void emit_radial_glow(ui::RenderQueue& queue, float cx, float cy, float radius,
                      std::uint32_t center_abgr, std::uint32_t edge_abgr) {
    constexpr std::size_t segments = 32;
    std::array<ColorVertex, segments * 3> verts{};
    for (std::size_t i = 0; i < segments; ++i) {
        const float a0 = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        const float a1 = (2.0f * kPi * static_cast<float>(i + 1)) / static_cast<float>(segments);
        verts[i * 3 + 0] = {cx, cy, center_abgr};
        verts[i * 3 + 1] = {cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, edge_abgr};
        verts[i * 3 + 2] = {cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, edge_abgr};
    }
    const std::uint32_t count = static_cast<std::uint32_t>(verts.size());
    auto* payload = queue.append(17, count * 12 + 20);
    const std::uint32_t primitive = 2; // D3DPT_TRIANGLELIST -> opcode 17 header 2 maps to triangle list? Wait: check backend.cpp!
    // backend.cpp:242: types = {POINTLIST(0), LINELIST(1), TRIANGLELIST(2), TRIANGLESTRIP(3), LINESTRIP(4)}
    std::memcpy(payload, &primitive, 4);
    std::memcpy(payload + 4, &count, 4);
    std::memcpy(payload + 8, verts.data(), verts.size() * sizeof(ColorVertex));
}

void draw_grenade_icon(ui::RenderQueue& queue, std::int32_t cx, std::int32_t cy,
                       const GrenadeWarningTexture& tex, GrenadeKind kind, std::uint32_t tint,
                       float scale) {
    if (tex.texture != 0xFFFFFFFFu && tex.width > 0 && tex.height > 0) {
        const auto w = static_cast<std::int32_t>(std::round(static_cast<float>(tex.width) * scale));
        const auto h = static_cast<std::int32_t>(std::round(static_cast<float>(tex.height) * scale));
        queue.texture({cx - w / 2, cy - h / 2, tex.texture, tint, 0, 0, tex.width, tex.height, 0,
                       scale});
    } else {
        queue.text(cx, cy, tint, 0x10 | 0x20, 0,
                   kind == GrenadeKind::explosive ? u"HE" : u"M");
    }
}

void draw_ground_ring(ui::RenderQueue& queue, const Projection& projection, Vector3 world_pos,
                      std::uint32_t color) {
    GrenadeScreenPoint ground{};
    if (project_grenade_point(projection, world_pos, ground)) {
        const auto gx = static_cast<std::int32_t>(std::round(ground.x));
        const auto gy = static_cast<std::int32_t>(std::round(ground.y));
        queue.circle_outline({gx, gy, scale_alpha(color, 0.45f), 8.0f, 0.0f, 1.0f, 1.5f});
        queue.circle({gx, gy, color, 2.5f, 0.0f, 1.0f});
    }
}

struct ClampGeometry {
    Point center{};
    bool clamped = false;
    float angle = 0.0f;
};

ClampGeometry compute_badge_position(const StylizedWarningInput& in, Point raw, bool in_front) {
    const float w = static_cast<float>(std::max(1, in.screen_width));
    const float h = static_cast<float>(std::max(1, in.screen_height));
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    float px = static_cast<float>(raw.x);
    float py = static_cast<float>(raw.y);
    if (!in_front) {
        px = w - px;
        py = h - py;
        // Push below center horizon if mirrored upward.
        if (py < cy)
            py = h - 42.0f;
    }
    const float margin = 46.0f;
    const float dx = px - cx;
    const float dy = py - cy;
    const float ang = std::atan2(dy, dx);
    bool was_clamped = !in_front || px < margin || px > (w - margin) || py < margin ||
                       py > (h - margin);
    if (!was_clamped)
        return {{static_cast<std::int32_t>(std::round(px)),
                 static_cast<std::int32_t>(std::round(py))},
                false,
                ang};

    // Ray-clamp onto screen inner rect.
    float rx = (dx == 0.0f) ? 1e6f : ((dx > 0.0f ? (w - margin - cx) : (margin - cx)) / dx);
    float ry = (dy == 0.0f) ? 1e6f : ((dy > 0.0f ? (h - margin - cy) : (margin - cy)) / dy);
    const float k = std::clamp(std::min(rx, ry), 0.0f, 1.0f);
    return {{static_cast<std::int32_t>(std::round(cx + dx * k)),
             static_cast<std::int32_t>(std::round(cy + dy * k))},
            true,
            ang};
}

void draw_pin_pointer(ui::RenderQueue& queue, float cx, float cy, float radius, float angle,
                      std::uint32_t color) {
    const float base_r = radius - 1.0f;
    const float tip_r = radius + 8.5f;
    const float spread = 0.42f;
    const auto x0 = static_cast<std::int32_t>(std::round(cx + std::cos(angle) * tip_r));
    const auto y0 = static_cast<std::int32_t>(std::round(cy + std::sin(angle) * tip_r));
    const auto x1 = static_cast<std::int32_t>(std::round(cx + std::cos(angle - spread) * base_r));
    const auto y1 = static_cast<std::int32_t>(std::round(cy + std::sin(angle - spread) * base_r));
    const auto x2 = static_cast<std::int32_t>(std::round(cx + std::cos(angle + spread) * base_r));
    const auto y2 = static_cast<std::int32_t>(std::round(cy + std::sin(angle + spread) * base_r));
    queue.triangle({x0, y0, x1, y1, x2, y2, color});
}

void draw_neverlose_style(ui::RenderQueue& queue, const StylizedWarningInput& in,
                          const ClampGeometry& geom, float fuse_fraction, float danger_ratio,
                          float alpha) {
    const float radius = 21.0f - (std::clamp(in.distance, 40.0f, 450.0f) - 40.0f) * (4.0f / 410.0f);
    const float cx = static_cast<float>(geom.center.x);
    const float cy = static_cast<float>(geom.center.y);

    const std::uint32_t danger_clr =
        lerp_abgr(abgr_rgba(90, 200, 255, 255), abgr_rgba(255, 65, 65, 255), danger_ratio);
    const std::uint32_t core_bg = scale_alpha(abgr_rgba(18, 20, 26, 232), alpha);
    const std::uint32_t track_ring = scale_alpha(abgr_rgba(45, 50, 60, 200), alpha);

    // Subtle radial halo behind badge scaled by threat.
    if (danger_ratio > 0.05f) {
        emit_radial_glow(queue, cx, cy, radius + 11.0f,
                         scale_alpha(danger_clr, 0.35f * alpha * danger_ratio),
                         abgr_rgba(255, 65, 65, 0));
    }

    if (geom.clamped)
        draw_pin_pointer(queue, cx, cy, radius + 1.5f, geom.angle, scale_alpha(danger_clr, alpha));

    queue.circle({geom.center.x, geom.center.y, core_bg, radius, 0.0f, 1.0f});
    queue.circle_outline({geom.center.x, geom.center.y, track_ring, radius, 0.0f, 1.0f, 2.2f});

    if (fuse_fraction > 0.02f) {
        queue.circle_outline({geom.center.x, geom.center.y, scale_alpha(danger_clr, alpha), radius,
                              -90.0f, fuse_fraction, 2.2f});
    }

    const bool show_dmg =
        (in.record.kind == GrenadeKind::explosive) && (in.record.damage >= 1.0f);
    draw_grenade_icon(queue, geom.center.x, geom.center.y - (show_dmg ? 4 : 0), in.texture,
                      in.record.kind, scale_alpha(abgr_rgba(245, 248, 255, 240), alpha), 0.72f);

    if (show_dmg) {
        const auto dmg_txt = u"-" + format_decimal_u16(static_cast<std::int32_t>(std::round(in.record.damage)));
        queue.text(geom.center.x, geom.center.y + 8,
                   scale_alpha(danger_ratio > 0.6f ? abgr_rgba(255, 95, 95, 255)
                                                   : abgr_rgba(255, 255, 255, 245),
                               alpha),
                   0x10 | 0x20 | 4, 0, dmg_txt);
    } else {
        const float rem = std::max(0.0f, in.record.end_time - in.current_time);
        if (rem > 0.0f && rem < 10.0f)
            queue.text(geom.center.x, geom.center.y + 9,
                       scale_alpha(abgr_rgba(220, 226, 238, 220), alpha), 0x10 | 0x20 | 4, 0,
                       format_timer_u16(rem));
    }
    draw_ground_ring(queue, in.projection, in.record.position, scale_alpha(danger_clr, alpha));
}

ClampGeometry compute_cs2_badge_position(const StylizedWarningInput& in, Point raw, bool in_front) {
    const float w = static_cast<float>(std::max(1, in.screen_width));
    const float h = static_cast<float>(std::max(1, in.screen_height));
    const float scale = h / 1080.0f;
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    float px = static_cast<float>(raw.x);
    float py = static_cast<float>(raw.y);
    if (!in_front) {
        px = w - px;
        py = h - py;
    }
    const float margin = 56.0f * scale;
    const float rx = std::max(32.0f, cx - margin);
    const float ry = std::max(32.0f, cy - margin);
    float dx = px - cx;
    float dy = py - cy;
    const float ellip = std::hypot(dx / rx, dy / ry);
    const bool clamped = !in_front || ellip > 1.0f;
    if (!in_front && ellip < 1e-3f) {
        dx = 0.0f;
        dy = ry;
    }
    const float ang = std::atan2(dy, dx);
    if (clamped) {
        const float k = 1.0f / std::max(std::hypot(dx / rx, dy / ry), 1e-4f);
        dx *= k;
        dy *= k;
    }
    return {{static_cast<std::int32_t>(std::round(cx + dx)),
             static_cast<std::int32_t>(std::round(cy + dy))},
            clamped,
            ang};
}

void draw_cs2_style(ui::RenderQueue& queue, const StylizedWarningInput& in,
                    const ClampGeometry& geom, float fuse_fraction, float danger_ratio,
                    float alpha) {
    (void)fuse_fraction;
    const float h = static_cast<float>(std::max(1, in.screen_height));
    const float scale = h / 1080.0f;
    const float glow_radius = 64.0f * scale;
    const float cx = static_cast<float>(geom.center.x);
    const float cy = static_cast<float>(geom.center.y);

    const float pulse = 0.5f - 0.5f * std::cos((2.0f * kPi / 0.65f) * in.current_time);
    const std::uint32_t calm = abgr_rgba(255, 205, 40, 255);
    const std::uint32_t danger_clr = abgr_rgba(235, 30, 30, 255);

    const bool is_he = (in.record.kind == GrenadeKind::explosive);
    const bool has_dmg = is_he && (in.record.damage >= 1.0f);
    float threat = 0.0f;
    if (is_he) {
        threat = has_dmg ? std::clamp(danger_ratio, 0.25f, 1.0f) : 0.0f;
    } else {
        const float dist_threat = std::clamp(1.0f - (in.distance / 480.0f), 0.12f, 1.0f);
        threat = (in.record.type == 2 || in.record.damage >= 1.0f)
                     ? std::max(dist_threat, 0.72f)
                     : dist_threat;
    }
    const float color_blend =
        is_he ? (has_dmg ? std::clamp(threat * (0.45f + 0.55f * pulse), 0.20f, 1.0f) : 0.0f)
              : std::clamp(threat * (0.50f + 0.50f * pulse), 0.0f, 1.0f);
    const std::uint32_t glow_clr = lerp_abgr(calm, danger_clr, color_blend);
    const float peak_alpha = (0.40f + 0.35f * (0.35f + 0.65f * threat) * pulse) * alpha;
    emit_gaussian_glow(queue, cx, cy, glow_radius, scale_alpha(glow_clr, peak_alpha));

    if (geom.clamped) {
        const float base_r = glow_radius * 0.54f;
        const float tip_r = glow_radius * 0.72f;
        const float spread = 0.28f;
        const auto x0 = static_cast<std::int32_t>(std::round(cx + std::cos(geom.angle) * tip_r));
        const auto y0 = static_cast<std::int32_t>(std::round(cy + std::sin(geom.angle) * tip_r));
        const auto x1 =
            static_cast<std::int32_t>(std::round(cx + std::cos(geom.angle - spread) * base_r));
        const auto y1 =
            static_cast<std::int32_t>(std::round(cy + std::sin(geom.angle - spread) * base_r));
        const auto x2 =
            static_cast<std::int32_t>(std::round(cx + std::cos(geom.angle + spread) * base_r));
        const auto y2 =
            static_cast<std::int32_t>(std::round(cy + std::sin(geom.angle + spread) * base_r));
        queue.triangle(
            {x0 + 1, y0 + 1, x1 + 1, y1 + 1, x2 + 1, y2 + 1, scale_alpha(abgr_rgba(0, 0, 0, 220), alpha)});
        queue.triangle({x0, y0, x1, y1, x2, y2, scale_alpha(abgr_rgba(255, 255, 255, 255), alpha)});
    }

    const float rem = std::max(0.0f, in.record.end_time - in.current_time);
    const bool has_time = rem > 0.0f && rem < 10.0f;
    const auto lines = (has_dmg ? 1 : 0) + (has_time ? 1 : 0);
    const std::int32_t icon_y =
        geom.center.y - (lines == 2 ? static_cast<std::int32_t>(std::round(11.0f * scale))
                                    : (lines == 1 ? static_cast<std::int32_t>(std::round(6.0f * scale)) : 0));

    const std::uint32_t white = scale_alpha(abgr_rgba(255, 255, 255, 255), alpha);
    draw_grenade_icon(queue, geom.center.x, icon_y, in.texture, in.record.kind, white,
                      std::clamp(0.76f * scale, 0.55f, 1.35f));

    std::int32_t text_y = geom.center.y + static_cast<std::int32_t>(std::round(8.0f * scale));
    if (has_dmg) {
        const auto txt =
            u"-" + format_decimal_u16(static_cast<std::int32_t>(std::round(in.record.damage))) + u" HP";
        queue.text(geom.center.x, text_y, white, 0x10 | 0x20 | 4, 0, txt);
        text_y += static_cast<std::int32_t>(std::round(12.0f * scale));
    }
    if (has_time) {
        queue.text(geom.center.x, text_y, white, 0x10 | 0x20 | 4, 0, format_timer_u16(rem, true));
    }
}

void draw_lunar_style(ui::RenderQueue& queue, const StylizedWarningInput& in,
                      const ClampGeometry& geom, float fuse_fraction, float danger_ratio,
                      float alpha) {
    const float radius = 20.0f;
    const float cx = static_cast<float>(geom.center.x);
    const float cy = static_cast<float>(geom.center.y);

    // Slate outer track + dark core + soft red center danger glow
    queue.circle({geom.center.x, geom.center.y, scale_alpha(abgr_rgba(16, 18, 24, 235), alpha),
                  radius, 0.0f, 1.0f});
    if (danger_ratio > 0.01f) {
        emit_radial_glow(queue, cx, cy, radius - 2.0f,
                         scale_alpha(abgr_rgba(255, 55, 55, 190), alpha * danger_ratio),
                         abgr_rgba(255, 55, 55, 0));
    }
    queue.circle_outline({geom.center.x, geom.center.y,
                          scale_alpha(abgr_rgba(45, 50, 62, 220), alpha), radius, 0.0f, 1.0f, 2.4f});
    if (fuse_fraction > 0.02f) {
        queue.circle_outline({geom.center.x, geom.center.y,
                              scale_alpha(abgr_rgba(255, 255, 255, 240), alpha), radius, -90.0f,
                              fuse_fraction, 2.4f});
    }
    if (geom.clamped) {
        draw_pin_pointer(queue, cx, cy, radius + 2.0f, geom.angle,
                         scale_alpha(danger_ratio > 0.4f ? abgr_rgba(255, 70, 70, 240)
                                                         : abgr_rgba(255, 255, 255, 220),
                                     alpha));
    }

    draw_grenade_icon(queue, geom.center.x, geom.center.y - 3, in.texture, in.record.kind,
                      scale_alpha(abgr_rgba(255, 255, 255, 245), alpha), 0.70f);
    const bool show_dmg =
        (in.record.kind == GrenadeKind::explosive) && (in.record.damage >= 1.0f);
    const auto txt = show_dmg
                         ? (u"-" + format_decimal_u16(static_cast<std::int32_t>(std::round(in.record.damage))))
                         : format_decimal_u16(static_cast<std::int32_t>(std::round(in.distance / 12.0f))) + u"m";
    queue.text(geom.center.x, geom.center.y + 9,
               scale_alpha(abgr_rgba(240, 240, 245, 240), alpha), 0x10 | 0x20 | 4, 0, txt);
}

void draw_legacy_style(ui::RenderQueue& queue, const StylizedWarningInput& in,
                       const ClampGeometry& geom, float fuse_fraction, float danger_ratio,
                       float alpha) {
    const float radius = 24.0f - (std::clamp(in.distance, 40.0f, 450.0f) - 40.0f) * (6.0f / 410.0f);
    const std::uint32_t rim =
        lerp_abgr(abgr_rgba(200, 200, 200, 220), abgr_rgba(255, 60, 60, 240), danger_ratio);
    queue.circle({geom.center.x, geom.center.y, scale_alpha(abgr_rgba(10, 10, 10, 215), alpha),
                  radius, 0.0f, 1.0f});
    if (danger_ratio > 0.01f) {
        emit_radial_glow(queue, static_cast<float>(geom.center.x),
                         static_cast<float>(geom.center.y), radius - 2.0f,
                         scale_alpha(abgr_rgba(255, 45, 45, 160), alpha * danger_ratio),
                         abgr_rgba(255, 45, 45, 0));
    }
    if (fuse_fraction > 0.02f) {
        queue.circle_outline({geom.center.x, geom.center.y, scale_alpha(rim, alpha), radius - 1.0f,
                              -90.0f, fuse_fraction, 2.0f});
    }
    draw_grenade_icon(queue, geom.center.x, geom.center.y - 4, in.texture, in.record.kind,
                      scale_alpha(abgr_rgba(255, 255, 255, 240), alpha), 0.75f);
    const std::int32_t val = in.record.kind == GrenadeKind::explosive
                                 ? static_cast<std::int32_t>(in.record.damage)
                                 : static_cast<std::int32_t>(std::round(in.distance / 12.0f));
    queue.text(geom.center.x, geom.center.y + 8, scale_alpha(abgr_rgba(255, 255, 255, 240), alpha),
               0x10 | 0x20, 0, format_decimal_u16(val));
}

struct Point2D {
    float x, y;
};

double cross2d(Point2D o, Point2D a, Point2D b) noexcept {
    return double(a.x - o.x) * double(b.y - o.y) - double(a.y - o.y) * double(b.x - o.x);
}

std::vector<Point2D> convex_hull(std::vector<Point2D> pts) {
    if (pts.size() <= 3)
        return pts;
    std::sort(pts.begin(), pts.end(), [](const Point2D& a, const Point2D& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    std::vector<Point2D> hull(pts.size() * 2);
    std::size_t k = 0;
    for (const auto& p : pts) {
        while (k >= 2 && cross2d(hull[k - 2], hull[k - 1], p) <= 0.0)
            --k;
        hull[k++] = p;
    }
    const std::size_t t = k + 1;
    for (std::size_t i = pts.size(); i > 0; --i) {
        const auto& p = pts[i - 1];
        while (k >= t && cross2d(hull[k - 2], hull[k - 1], p) <= 0.0)
            --k;
        hull[k++] = p;
    }
    if (k > 1)
        hull.resize(k - 1);
    return hull;
}

} // namespace

void draw_stylized_grenade_warning(ui::RenderQueue& queue, const StylizedWarningInput& in) {
    const bool cs2 = (in.config.proximity_style.value == 2);
    const float max_dist =
        cs2 ? 480.0f : ((in.record.kind == GrenadeKind::explosive) ? 600.0f : 420.0f);
    if (in.distance > max_dist)
        return;

    Point raw{};
    bool front = false;
    if (!project_grenade_warning(in.projection, in.record.position, raw, front))
        return;

    const float dist_alpha =
        (!cs2 && in.distance > max_dist - 100.0f)
            ? std::clamp((max_dist - in.distance) / 100.0f, 0.0f, 1.0f)
            : 1.0f;
    if (dist_alpha <= 0.02f)
        return;

    const float duration = in.record.end_time - in.record.creation_time;
    float fuse_fraction = 1.0f;
    if (duration > 0.0f && duration < 8.5f) {
        const float elapsed = std::clamp((in.current_time - in.record.creation_time) / duration, 0.0f, 1.0f);
        fuse_fraction = 1.0f - elapsed;
    }
    const float denom = std::min(80.0f, static_cast<float>(std::max(1, in.local_health)));
    const float danger_ratio = std::clamp(in.record.damage / denom, 0.0f, 1.0f);
    const ClampGeometry geom = cs2 ? compute_cs2_badge_position(in, raw, front)
                                   : compute_badge_position(in, raw, front);

    switch (in.config.proximity_style.value) {
    case 1:
        draw_neverlose_style(queue, in, geom, fuse_fraction, danger_ratio, dist_alpha);
        break;
    case 2:
        draw_cs2_style(queue, in, geom, fuse_fraction, danger_ratio, dist_alpha);
        break;
    case 3:
        draw_lunar_style(queue, in, geom, fuse_fraction, danger_ratio, dist_alpha);
        break;
    case 4:
    default:
        draw_legacy_style(queue, in, geom, fuse_fraction, danger_ratio, dist_alpha);
        break;
    }
}

namespace {
void emit_multi_band_polygon_hull(ui::RenderQueue& queue, Point2D center,
                                  const std::vector<Point2D>& hull, std::uint32_t base_clr,
                                  float fade_fraction) {
    constexpr int kBands = 12;
    const std::size_t edges = hull.size();
    if (edges < 3 || fade_fraction <= 0.01f)
        return;

    const auto band_scale = [](int step) noexcept -> float {
        if (step <= 0)
            return 0.0f;
        return std::cbrt(static_cast<float>(step) / static_cast<float>(kBands));
    };
    const auto band_color = [&](float r) noexcept -> std::uint32_t {
        // Smooth radial ramp peaking at ~50% opacity at the hull perimeter.
        const float opacity = (0.14f + 0.36f * std::pow(std::clamp(r, 0.0f, 1.0f), 1.6f)) *
                              fade_fraction;
        return scale_alpha(base_clr | 0xFF000000u, opacity);
    };

    const std::uint32_t verts_total =
        static_cast<std::uint32_t>(edges * 3 + (kBands - 1) * edges * 6);
    {
        auto* payload = queue.append(17, verts_total * 12 + 20);
        const std::uint32_t primitive = 2; // D3DPT_TRIANGLELIST
        std::memcpy(payload, &primitive, 4);
        std::memcpy(payload + 4, &verts_total, 4);
        payload += 8;

        for (int step = 1; step <= kBands; ++step) {
            const float r0 = band_scale(step - 1);
            const float r1 = band_scale(step);
            const std::uint32_t c0 = band_color(r0);
            const std::uint32_t c1 = band_color(r1);
            for (std::size_t i = 0; i < edges; ++i) {
                const auto& a = hull[i];
                const auto& b = hull[(i + 1) % edges];
                const Point2D o0{center.x + (a.x - center.x) * r1, center.y + (a.y - center.y) * r1};
                const Point2D o1{center.x + (b.x - center.x) * r1, center.y + (b.y - center.y) * r1};
                if (step == 1) {
                    const ColorVertex v[3]{{center.x, center.y, c0}, {o0.x, o0.y, c1}, {o1.x, o1.y, c1}};
                    std::memcpy(payload, v, sizeof(v));
                    payload += sizeof(v);
                } else {
                    const Point2D i0{center.x + (a.x - center.x) * r0,
                                     center.y + (a.y - center.y) * r0};
                    const Point2D i1{center.x + (b.x - center.x) * r0,
                                     center.y + (b.y - center.y) * r0};
                    const ColorVertex v[6]{{i0.x, i0.y, c0}, {o0.x, o0.y, c1}, {o1.x, o1.y, c1},
                                           {i0.x, i0.y, c0}, {o1.x, o1.y, c1}, {i1.x, i1.y, c0}};
                    std::memcpy(payload, v, sizeof(v));
                    payload += sizeof(v);
                }
            }
        }
    }

    const std::uint32_t outline_clr = scale_alpha(base_clr | 0xFF000000u, 0.85f * fade_fraction);
    {
        const std::uint32_t vert_count = static_cast<std::uint32_t>(edges + 1);
        auto* payload = queue.append(17, vert_count * 12 + 20);
        const std::uint32_t primitive = 4; // D3DPT_LINESTRIP
        std::memcpy(payload, &primitive, 4);
        std::memcpy(payload + 4, &vert_count, 4);
        payload += 8;
        for (std::size_t i = 0; i <= edges; ++i) {
            const auto& p = hull[i % edges];
            const ColorVertex v{p.x, p.y, outline_clr};
            std::memcpy(payload, &v, sizeof(v));
            payload += sizeof(v);
        }
    }
}
} // namespace

void draw_molotov_area_hull(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                            const Projection& projection, std::span<const Vector3> world_cells,
                            bool harmful, float fade_fraction) {
    if (!config.molotov_area.value || world_cells.empty() || fade_fraction <= 0.01f)
        return;

    std::vector<Point2D> ring_pts;
    ring_pts.reserve(world_cells.size() * 8);
    constexpr float cell_radius = 60.0f;
    for (const auto& cell : world_cells) {
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * (kPi * 0.25f);
            const Vector3 sample{cell.x + std::cos(a) * cell_radius,
                                 cell.y + std::sin(a) * cell_radius, cell.z};
            GrenadeScreenPoint pt{};
            if (project_grenade_point(projection, sample, pt))
                ring_pts.push_back({pt.x, pt.y});
        }
    }
    if (ring_pts.size() < 3)
        return;

    const auto hull = convex_hull(std::move(ring_pts));
    if (hull.size() < 3)
        return;

    Point2D centroid{0.0f, 0.0f};
    for (const auto& p : hull) {
        centroid.x += p.x;
        centroid.y += p.y;
    }
    centroid.x /= static_cast<float>(hull.size());
    centroid.y /= static_cast<float>(hull.size());

    const std::uint32_t base_clr =
        harmful ? config.molotov_harmful_color.value : config.molotov_safe_color.value;
    emit_multi_band_polygon_hull(queue, centroid, hull, base_clr, fade_fraction);
}

void draw_smoke_area(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                     const Projection& projection, Vector3 origin, float fade_fraction) {
    if (!config.smoke_area.value || fade_fraction <= 0.01f)
        return;

    constexpr int segments = 32;
    constexpr float smoke_radius = 144.0f;
    std::vector<Point2D> ring_pts;
    ring_pts.reserve(segments);
    for (int k = 0; k < segments; ++k) {
        const float a = (2.0f * kPi * static_cast<float>(k)) / static_cast<float>(segments);
        const Vector3 sample{origin.x + std::cos(a) * smoke_radius,
                             origin.y + std::sin(a) * smoke_radius, origin.z};
        GrenadeScreenPoint pt{};
        if (project_grenade_point(projection, sample, pt))
            ring_pts.push_back({pt.x, pt.y});
    }
    if (ring_pts.size() < 3)
        return;

    const auto hull = convex_hull(std::move(ring_pts));
    if (hull.size() < 3)
        return;

    Point2D center_pt{0.0f, 0.0f};
    GrenadeScreenPoint origin_2d{};
    if (project_grenade_point(projection, origin, origin_2d)) {
        center_pt = {origin_2d.x, origin_2d.y};
    } else {
        for (const auto& p : hull) {
            center_pt.x += p.x;
            center_pt.y += p.y;
        }
        center_pt.x /= static_cast<float>(hull.size());
        center_pt.y /= static_cast<float>(hull.size());
    }

    emit_multi_band_polygon_hull(queue, center_pt, hull, config.smoke_area_color.value,
                                 fade_fraction);
}

} // namespace skeet::esp
