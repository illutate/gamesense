#include "skeet/hud/screen_overlay.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <immintrin.h>
#include <vector>

namespace skeet::hud {
namespace {
struct Vertex {
    float x, y;
    std::uint32_t color;
};
// Positive radii enter433EC12E after multiplication by pi. That CRT helper
// truncates modulo2^32, returning zero for non-finite values.
std::uint32_t native_integer(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto exponent = std::int32_t((bits >> 23) & 255) - 127;
    if (exponent < 0 || exponent >= 55 || (bits & 0x7f800000u) == 0x7f800000u)
        return 0;
    const auto mantissa = (bits & 0x007fffffu) | 0x00800000u;
    const auto integer = exponent >= 23 ? mantissa << std::uint32_t(exponent - 23)
                                        : mantissa >> std::uint32_t(23 - exponent);
    return bits >> 31 ? 0u - integer : integer;
}
float integer_float(std::int32_t value) {
    return _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(value)));
}
std::int32_t displacement(std::int32_t pixel, float center) {
    const auto difference = _mm_cvttss_si32(_mm_set_ss(integer_float(pixel) - center));
    const auto sign = std::uint32_t(difference >> 31);
    const auto absolute = std::bit_cast<std::int32_t>((std::uint32_t(difference) ^ sign) - sign);
    return absolute >= 2 ? difference : 0;
}
}
void draw_screen_overlay(ui::RenderQueue& queue, const esp::ScreenOverlayConfig& config,
                         const ScreenOverlayState& state, void* local, std::uint32_t life_offset,
                         Point screen_size, const esp::Projection& projection,
                         const RageVector& origin, const RageTrigonometry& trig) {
    if ((!config.inaccuracy.value && !config.recoil.value) || !local)
        return;
    std::uint8_t life;
    std::memcpy(&life, static_cast<std::byte*>(local) + life_offset, sizeof(life));
    if (life)
        return;
    float x = integer_float(screen_size.x >> 1), y = integer_float(screen_size.y >> 1);
    if (config.recoil.value && state.byte_6c && !*state.byte_58) {
        const auto* angle = engine::invoke<const float*>(local, 171);
        const std::array<float, 3> copy{angle[0], angle[1], angle[2]};
        const auto yaw = (state.recoil_yaw_64 + copy[1]) * 0.01745329238474369f;
        const auto sy = trig.sine(yaw), cy = trig.cosine(yaw);
        const auto pitch = (state.recoil_pitch_60 + copy[0]) * 0.01745329238474369f;
        const auto sp = trig.sine(pitch), cp = trig.cosine(pitch);
        const esp::Vector3 point{(cp * cy) * 10000.0f + origin[0], (cp * sy) * 10000.0f + origin[1],
                                 (-sp) * 10000.0f + origin[2]};
        Point projected;
        if (esp::project_point(projection, point, projected)) {
            x += integer_float(displacement(projected.x, x));
            y += integer_float(displacement(projected.y, y));
        }
    }
    float radius;
    std::uint32_t segments;
    if (config.inaccuracy.value) {
        radius = state.radius_5c;
        if (radius < 4.0f)
            return;
        segments = std::min(native_integer(radius * 3.1415927410125732f), 512u);
    } else {
        if (!state.byte_6c)
            return;
        radius = 10.0f;
        segments = 31;
    }
    const auto step = float(6.283185307179586 / double(float(segments)));
    auto sine = trig.sine(0), cosine = trig.cosine(0);
    auto last_x = cosine * radius + x + 0.5f, last_y = sine * radius + y + 0.5f;
    const auto color = config.inaccuracy_color.value;
    std::vector<Vertex> vertices;
    vertices.reserve(std::size_t(segments) * 3);
    for (std::uint32_t i = 0; i < segments; ++i) {
        vertices.push_back({last_x, last_y, color});
        const auto angle = float(i + 1) * step;
        sine = trig.sine(angle);
        cosine = trig.cosine(angle);
        last_x = cosine * radius + x + 0.5f;
        last_y = sine * radius + y + 0.5f;
        vertices.push_back({last_x, last_y, color});
        vertices.push_back({x, y, color & 0x00FFFFFFu});
    }
    const auto count = segments * 3;
    auto* payload = queue.append(7, count * sizeof(Vertex) + 16);
    std::memcpy(payload, &count, sizeof(count));
    if (!vertices.empty())
        std::memcpy(payload + 4, vertices.data(), vertices.size() * sizeof(Vertex));
}
} // namespace skeet::hud
