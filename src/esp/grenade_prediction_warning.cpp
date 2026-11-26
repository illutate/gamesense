#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/render/backend.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/color_picker.hpp"
#include "detail.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace skeet::esp {
namespace {
float clamp(float value, float low, float high) {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(low)), _mm_set_ss(high)));
}
std::int32_t truncate(float value) {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
std::int32_t nearest(float value) {
    return _mm_cvtss_si32(_mm_set_ss(value));
}
std::int32_t edge(std::int32_t value, std::int32_t size) {
    const auto maximum = std::bit_cast<std::int32_t>(std::uint32_t(size) - 50u);
    if (maximum < 50)
        return maximum;
    return std::clamp(value, 50, maximum);
}
float separation(Vector3 a, Vector3 b) {
    const auto x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt((x * x + y * y) + z * z);
}
std::uint32_t damage_color(float amount) { // 433CF469 -> existing native HSV helpers
    if (amount <= 0)
        return 0xbe323232u;
    if (amount >= 1)
        return 0xbe0000ffu;
    const auto first = ui::rgb_to_hsv(0xbe323232u), second = ui::rgb_to_hsv(0xbe0000ffu);
    return ui::hsv_to_rgb({(second.hue - first.hue) * amount + first.hue,
                           (second.saturation - first.saturation) * amount + first.saturation,
                           (second.value - first.value) * amount + first.value});
}
struct Vertex {
    float x, y;
    std::uint32_t color;
};
void damage_fan(ui::RenderQueue& queue, Point point, float fraction, float warning_radius) {
    const auto color = damage_color(std::sqrt(fraction)), outer = color & 0xffffffu;
    const auto angle = std::bit_cast<float>(0x3dcf8c3du);
    const auto cosine = std::cos(angle), sine = std::sin(angle);
    const auto radius = warning_radius - 4.0f;
    const auto cx = static_cast<float>(point.x), cy = static_cast<float>(point.y);
    auto x = std::cos(0.0f), y = std::sin(0.0f);
    std::array<Vertex, 128> vertices{};
    vertices[0] = {cx, cy, color};
    vertices[1] = {cx, cy, color};
    vertices[2] = {x * radius + cx, y * radius + cy, outer};
    float last_x = vertices[2].x, last_y = vertices[2].y;
    for (unsigned i = 0; i != 62; ++i) {
        const auto next_x = x * cosine - y * sine;
        y = y * cosine + x * sine;
        x = next_x;
        last_x = x * radius + cx;
        last_y = y * radius + cy;
        vertices[3 + i * 2] = {cx, cy, color};
        vertices[4 + i * 2] = {last_x, last_y, outer};
    }
    vertices[127] = {last_x, last_y, outer};
    auto* payload =
        queue.append(17, static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex) + 20));
    const std::uint32_t header[]{3, 128};
    std::memcpy(payload, header, 8);
    std::memcpy(payload + 8, vertices.data(), sizeof(vertices));
}
}
bool project_grenade_warning(const Projection& projection, Vector3 position, Point& point,
                             bool& in_front) noexcept {
    const auto& m = projection.matrix;
    auto w = (position.x * m[12] + m[13] * position.y) + position.z * m[14] + m[15];
    if (w == 0.0f) {
        point = {truncate(projection.horizontal_scale), truncate(projection.vertical_scale * 2.0f)};
        return false;
    }
    in_front = true;
    if (w < 0.0f) {
        w = -w;
        in_front = false;
    }
    const auto reciprocal = 1.0f / w;
    const auto horizontal = ((position.y * m[1] + position.x * m[0]) + position.z * m[2]) + m[3];
    const auto vertical = ((position.y * m[5] + position.x * m[4]) + position.z * m[6]) + m[7];
    point = {nearest((reciprocal * horizontal - -1.0f) * projection.horizontal_scale),
             nearest((1.0f - vertical * reciprocal) * projection.vertical_scale)};
    return true;
}
namespace {
struct FixedWarningView {
    const GrenadeWarningFrame& frame;
    GrenadeWarningTexture image;
    std::int32_t width() const {
        return frame.width;
    }
    std::int32_t height() const {
        return frame.height;
    }
    std::int32_t health() const {
        return frame.health;
    }
    float now() const {
        return (float(frame.tick_count) * frame.tick_interval - frame.tick_interval) +
               frame.interpolation * frame.tick_interval;
    }
    GrenadeWarningTexture texture(GrenadeKind) const {
        return image;
    }
};
struct LiveWarningView {
    const GrenadeWarningLiveFrame& frame;
    std::int32_t width() const {
        return frame.renderer.screen_width;
    }
    std::int32_t height() const {
        return frame.renderer.screen_height;
    }
    std::int32_t health() const {
        return engine::invoke<std::int32_t>(frame.player, 122);
    }
    float now() const {
        return (float(frame.tick_count) * frame.tick_interval - frame.tick_interval) +
               frame.interpolation * frame.tick_interval;
    }
    GrenadeWarningTexture texture(GrenadeKind kind) const {
        auto image = kind == GrenadeKind::explosive ? frame.explosive_texture : frame.fire_texture;
        if (image.texture != 0xffffffffu)
            frame.renderer.texture_size(image.texture, &image.width, &image.height);
        return image;
    }
};
template <class View>
void warning(ui::RenderQueue& queue, const Projection& projection,
             const GrenadeWarningRecord& record, float distance, const View& view) {
    if (!(distance <= 400.0f))
        return;
    const auto width = view.width(), height = view.height();
    Point point{};
    bool front = false;
    if (!project_grenade_warning(projection, record.position, point, front))
        return;
    if (!front)
        point.y = height;
    point = {edge(point.x, width), edge(point.y, height)};
    const auto radius = std::round(50.0f - ((clamp(distance, 36, 200) - 36.0f) * 20.0f) / 164.0f);
    queue.circle({point.x, point.y, 0xd2000000u, radius, 0, 1});
    if (record.damage > 0) {
        const auto health = view.health();
        if (health > 0) {
            const auto fraction =
                clamp(record.damage / std::min(50.0f, static_cast<float>(health)), 0, 1);
            if (fraction > 0)
                damage_fan(queue, point, fraction, radius);
        }
    }
    const auto duration = record.end_time - record.creation_time;
    if (duration > 0 && duration < 8.0f) {
        const auto now = view.now();
        const auto fraction = clamp((now - record.creation_time) / duration, 0, 1);
        queue.circle_outline({point.x, point.y, 0xc8ffffffu, radius - 1.0f, 0, 1.0f - fraction, 2});
    }
    const auto texture = view.texture(record.kind);
    if (texture.texture == 0xffffffffu) {
        queue.text(point.x, detail::sub(point.y, 10), 0xc828ffffu, 0x12, 0, u"!");
        point.y = detail::add(point.y, 10);
    } else {
        queue.texture({detail::sub(point.x, texture.width / 2),
                       detail::sub(detail::sub(point.y, texture.height / 2), 10), texture.texture,
                       0xc8ffffffu, 0, 0, texture.width, texture.height, 0, 1});
        point.y = detail::add(point.y, texture.height / 2);
    }
    const auto value =
        record.kind == GrenadeKind::explosive ? truncate(record.damage) : nearest(distance / 12.0f);
    queue.text(point.x, point.y, 0xc8ffffffu, 0x11, 0, detail::decimal(value));
}
}
void draw_grenade_warning(ui::RenderQueue& queue, const Projection& projection,
                          const GrenadeWarningRecord& record, float distance,
                          const GrenadeWarningFrame& frame, GrenadeWarningTexture image) {
    warning(queue, projection, record, distance, FixedWarningView{frame, image});
}
void draw_grenade_warning(ui::RenderQueue& queue, const Projection& projection,
                          const GrenadeWarningRecord& record, float distance,
                          const GrenadeWarningLiveFrame& frame) {
    warning(queue, projection, record, distance, LiveWarningView{frame});
}
void draw_grenade_warnings(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                           const Projection& projection, std::span<GrenadeWarningRecord> records,
                           std::span<const GrenadeWarningEntity> entities,
                           const GrenadeWarningFrame& frame, GrenadeWarningTexture explosive,
                           GrenadeWarningTexture fire) {
    if (!config.proximity_warning.value || !frame.local_present || frame.life_state != 0)
        return;
    if (frame.movement_type == 10 && (frame.observer_mode == 4 || frame.observer_mode == 5))
        return;
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto& record = records[i];
        if (record.handle == 0xffffffffu || i >= entities.size() || !entities[i].present ||
            entities[i].effects_bit5) {
            record.handle = 0xffffffffu;
            continue;
        }
        const auto& entity = entities[i];
        if ((record.type <= 1 && entity.explosion_effect_tick != 0) || record.suppressed)
            continue;
        auto distance =
            separation(record.type == 2 ? entity.origin : record.position, frame.local_position);
        if (record.type == 2)
            for (const auto& cell : entity.fire_cells) {
                const auto next = separation(cell, frame.local_position);
                distance = _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(next), _mm_set_ss(distance)));
            }
        draw_grenade_warning(queue, projection, record, distance, frame,
                             record.kind == GrenadeKind::explosive ? explosive : fire);
    }
}
} // namespace skeet::esp
