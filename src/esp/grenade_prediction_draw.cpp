#include "skeet/esp/grenade_prediction.hpp"
#include <immintrin.h>
#include <cstring>
#include <stdexcept>

namespace skeet::esp {
bool project_grenade_point(const Projection& p, Vector3 v, GrenadeScreenPoint& output) noexcept {
    const auto& m = p.matrix;
    const auto w = ((v.x * m[12] + v.y * m[13]) + v.z * m[14]) + m[15];
    if (0.001f > w)
        return false;
    const auto reciprocal = 1.0f / w;
    const auto x = ((v.x * m[0] + v.y * m[1]) + v.z * m[2]) + m[3];
    const auto y = ((v.x * m[4] + v.y * m[5]) + v.z * m[6]) + m[7];
    output = {(x * reciprocal) * p.horizontal_scale + p.horizontal_scale,
              p.vertical_scale - (y * reciprocal) * p.vertical_scale};
    return true;
}
void record_grenade_point(GrenadePath& path, const Projection& projection, Vector3 position,
                          bool bounce) {
    GrenadeScreenPoint point;
    if (!project_grenade_point(projection, position, point))
        return;
    path.points.push_back(point);
    if (bounce && path.bounces.size() < 20)
        path.bounces.push_back(point);
}
void draw_active_grenade_path(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                              const GrenadePath& path, const GrenadeScreenPoint* endpoint,
                              std::int32_t damage) {
    if (path.points.empty())
        return;
    const auto color =
        damage > 0 ? config.trajectory_secondary_color.value : config.trajectory_color.value;
    auto truncate = [](float value) { return _mm_cvttss_si32(_mm_set_ss(value)); };
    // Native 433F22FC is a float vertex stream (opcode 17, primitive 4).
    // Integer polyline opcode 5 would discard the projected subpixel positions.
    if (path.points.size() > (0x07ffffffu - 24u) / 12u)
        throw std::length_error("Grenade path exceeds render command size");
    const auto count = static_cast<std::uint32_t>(path.points.size());
    auto* payload = queue.append(17, count * 12 + 20);
    const std::uint32_t primitive = 4;
    std::memcpy(payload, &primitive, 4);
    std::memcpy(payload + 4, &count, 4);
    for (const auto& point : path.points) {
        std::memcpy(payload + 8, &point, 8);
        std::memcpy(payload + 16, &color, 4);
        payload += 12;
    }
    for (const auto& bounce : path.bounces) {
        const auto x = truncate(bounce.x), y = truncate(bounce.y);
        queue.circle({x, y, 0x3c000000u, 3.0f, 0.0f, 1.0f});
        queue.circle({x, y, 0xa0ffffffu, 2.0f, 0.0f, 1.0f});
    }
    if (endpoint) {
        const auto x = truncate(endpoint->x), y = truncate(endpoint->y);
        queue.circle({x, y, 0x3c000000u, 3.0f, 0.0f, 1.0f});
        queue.circle({x, y, damage > 0 ? 0xff28ff00u : 0xff2800ffu, 2.0f, 0.0f, 1.0f});
    }
}
void draw_grenade_path(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                       const GrenadePath& path, const Projection& projection, Vector3 position,
                       std::int32_t damage) {
    if (!config.trajectory.value || path.points.empty())
        return;
    GrenadeScreenPoint endpoint;
    const auto visible = project_grenade_point(projection, position, endpoint);
    draw_active_grenade_path(queue, config, path, visible ? &endpoint : nullptr, damage);
}
} // namespace skeet::esp
