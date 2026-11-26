#include "skeet/esp/offscreen.hpp"
#include "detail.hpp"
#include "skeet/resolver.hpp"

namespace skeet::esp {
using namespace detail;

void draw_out_of_fov_arrow(ui::RenderQueue& q, Point screen, i32 size_value, i32 distance_value,
                           std::uint32_t color, float yaw, float time) {
    if (std::isnan(yaw))
        yaw = 0.0f;
    else {
        if (std::fabs(yaw) > 180.0f) {
            const auto turns = round_half_away_from_zero(std::fabs(yaw / 360.0f)) * 360.0f;
            yaw = yaw < 0.0f ? yaw + turns : yaw - turns;
        }
        if (yaw > 0.0f)
            yaw = sse_max(90.0f, yaw);
        else if (yaw < 0.0f)
            yaw = sse_min(-90.0f, yaw);
    }
    const float angle = yaw * std::bit_cast<float>(0x3C8EFA35u);
    // imports.h callsite evidence: 0x7750C6D0 is sinf; 0x7750A770 is cosf.
    const float cosine = std::cos(angle), negative_sine = -std::sin(angle);
    const float cx = float(screen.x) * 0.5f, cy = float(screen.y) * 0.5f;
    const float size = float(size_value);
    const float radius = (float(distance_value) * 0.01f) * (cy - size);
    const float xr = (cx / cy) * radius;
    const float half_size = size * 0.5f;
    const auto x0 = truncate((negative_sine * xr + cx) + cosine * half_size);
    const auto y0 = truncate((cy - cosine * radius) + negative_sine * half_size);
    const auto x1 = truncate(float(x0) - cosine * size);
    const auto y1 = truncate(float(y0) - negative_sine * size);
    const auto x2 = truncate((xr + size) * negative_sine + cx);
    const auto y2 = truncate(cy - (radius + size) * cosine);
    const float pulse = std::cos(time * 5.0f) * 117.0f + 137.0f;
    const float alpha = float(color >> 24) * std::bit_cast<float>(0x3B808081u);
    color = (color & 0xFFFFFFu) | ((std::uint32_t(truncate(pulse * alpha)) & 255) << 24);
    q.triangle({x0, y0, x1, y1, x2, y2, color});
}

void draw_offscreen_indicator(ui::RenderQueue& q, const OverlayConfig& c, const OffscreenInput& p,
                              LocalPlayerPredicate predicate) {
    if (!c.out_of_fov.value || !predicate.evaluate(predicate.context))
        return;
    draw_selected_offscreen_indicator(q, c, p);
}
void draw_selected_offscreen_indicator(ui::RenderQueue& q, const OverlayConfig& c,
                                       const OffscreenInput& p) {
    const float aspect = (float(p.screen.x) / float(p.screen.y)) * 0.75f;
    const float half_fov = convert_field_of_view(p.camera_fov, aspect) * 0.5f;
    const float yaw_radians = p.yaw * std::bit_cast<float>(0x3C8EFA35u);
    const float pitch_radians = p.pitch * std::bit_cast<float>(0x3C8EFA35u);
    const float pitch_cosine = std::cos(pitch_radians);
    const Vector3 axis{pitch_cosine * std::cos(yaw_radians), pitch_cosine * std::sin(yaw_radians),
                       -std::sin(pitch_radians)};
    if (half_fov >= angle_to_target(p.origin, p.target, axis))
        return;
    const float dx = p.target.x - p.origin.x, dy = p.target.y - p.origin.y;
    float target_yaw = 0.0f;
    if (dx != 0.0f || dy != 0.0f)
        target_yaw = float(std::atan2(double(dy), double(dx))) * std::bit_cast<float>(0x42652EE0u);
    draw_out_of_fov_arrow(q, p.screen, c.arrow_size.value, c.arrow_distance.value,
                          c.arrow_color.value, target_yaw - p.yaw, p.animation_time);
}

} // namespace skeet::esp
