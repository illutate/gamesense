#include "skeet/render/backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skeet::ui {
namespace {
constexpr float tau = 6.283185482025146484375f;
constexpr float radians = 0.01745329238474369049f;
constexpr float step = 0.2094395160675048828125f;
std::uint32_t color(std::uint32_t value) noexcept {
    return (value & 0xff00ff00U) | ((value & 255) << 16) | ((value >> 16) & 255);
}
std::int32_t truncate(float value) noexcept {
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return INT32_MIN;
    return static_cast<std::int32_t>(value);
}
}

// 433E9526: independent 12-degree wedges, capped at 31 triangles. The native
// routine applies alpha here and again when the vertex buffer is flushed.
void RendererBackend::circle(CircleCommand command) {
    if (command.radius < 2.0f || command.radius > 1000.0f || command.fraction <= 0.0f)
        return;
    if (alpha_ != 1.0f)
        command.color =
            (command.color & 0x00ffffffU) |
            (std::uint32_t(std::uint8_t(truncate(float(command.color >> 24) * alpha_))) << 24);
    if (!prepare(D3DPT_TRIANGLELIST, nullptr, 93, true))
        return;
    const auto packed = color(command.color);
    auto angle = command.rotation * radians;
    const auto end = std::min(command.fraction, 1.0f) * tau + angle;
    auto cosine = std::cos(angle), sine = std::sin(angle);
    for (std::int32_t count = 3; count <= 93; count += 3) {
        angle = angle + step;
        vertex(sine * command.radius + float(command.x), cosine * command.radius + float(command.y),
               packed);
        const bool finished = angle >= end;
        if (finished)
            angle = end;
        cosine = std::cos(angle);
        sine = std::sin(angle);
        vertex(sine * command.radius + float(command.x), cosine * command.radius + float(command.y),
               packed);
        vertex(float(command.x), float(command.y), packed);
        if (finished)
            break;
    }
}

// 4334A4D6: connected strip with duplicate first and last vertices. Segment
// rotation uses float recurrence, preserving the original accumulated error.
void RendererBackend::circle_outline(CircleOutlineCommand command) {
    if (command.radius < 2.0f || command.radius > 1000.0f || command.fraction <= 0.0f)
        return;
    const auto arc = std::min(command.fraction, 1.0f) * tau;
    auto segments = truncate(float(std::floor(double(arc / step + 2.0f))));
    if (segments > 0x3fd)
        segments = 0x3fd;
    if (!prepare(D3DPT_TRIANGLESTRIP, nullptr, std::uint32_t(segments) * 2 + 4, true))
        return;
    const auto delta = arc / float(segments);
    const auto dc = std::cos(delta), ds = std::sin(delta);
    auto cosine = std::cos(command.rotation * radians), sine = std::sin(command.rotation * radians);
    const auto inner = command.radius - command.thickness;
    const auto packed = color(command.color);
    const auto cx = float(command.x), cy = float(command.y);
    auto emit = [&](float radius) { vertex(cosine * radius + cx, sine * radius + cy, packed); };
    emit(inner);
    emit(inner);
    emit(command.radius);
    for (std::int32_t i = 0; i < segments; ++i) {
        const auto old_cosine = cosine;
        cosine = cosine * dc - sine * ds;
        sine = sine * dc + old_cosine * ds;
        emit(inner);
        emit(command.radius);
    }
    emit(command.radius);
}
} // namespace skeet::ui
