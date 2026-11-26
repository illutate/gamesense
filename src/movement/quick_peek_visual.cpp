#include "skeet/movement/quick_peek.hpp"
#include "skeet/esp/projection.hpp"
#include "skeet/render_queue.hpp"
#include "movement_math.hpp"
#include <cstring>

namespace skeet {
namespace {
struct Vertex {
    float x, y;
    std::uint32_t color;
};
bool project(const esp::Projection& p, MovementVector position, Vertex& output) {
    const auto& m = p.matrix;
    const float w = ((position.y * m[13] + position.x * m[12]) + position.z * m[14]) + m[15];
    if (0.001f > w)
        return false;
    const float reciprocal = 1.0f / w;
    const float x = ((position.y * m[1] + position.x * m[0]) + position.z * m[2]) + m[3];
    const float y = ((position.y * m[5] + position.x * m[4]) + position.z * m[6]) + m[7];
    output.x = (x * reciprocal) * p.horizontal_scale + p.horizontal_scale;
    output.y = p.vertical_scale - (y * reciprocal) * p.vertical_scale;
    return true;
}
}
void draw_quick_peek(const QuickPeekConfig& config, InputBitmap input, const QuickPeekState& state,
                     const esp::Projection& projection, ui::RenderQueue& queue) {
    if (!config.enabled.value || !hotkey_active(config.hotkey.value.binding, input) ||
        !state.origin.valid)
        return;
    // Native 4337DCA0 computes both tangent vectors even for this fixed plane.
    const float pitch = -90.0f * movement_detail::degrees_to_radians;
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(0.0f), cy = std::cos(0.0f);
    const float sr = std::sin(0.0f), cr = std::cos(0.0f);
    const MovementVector first{(cr * cy) * sp + sr * sy, (cr * sy) * sp - sr * cy, cr * cp};
    const MovementVector second{(sr * cy) * sp - cr * sy, (sr * sy) * sp + cr * cy, sr * cp};
    const auto origin = state.origin.value;
    MovementVector previous{first.x * 24.0f + origin.x, first.y * 24.0f + origin.y,
                            first.z * 24.0f + origin.z};
    Vertex center{0, 0, config.color.value};
    if (!project(projection, origin, center))
        return;
    std::array<Vertex, 48> vertices;
    for (int i = 1; i <= 16; ++i) {
        const float angle = static_cast<float>(i) * 0.3926990926265716552734375f;
        const float sine = std::sin(angle), cosine = std::cos(angle);
        const MovementVector next{
            ((first.x * cosine) * 24.0f + origin.x) + (second.x * sine) * 24.0f,
            ((first.y * cosine) * 24.0f + origin.y) + (second.y * sine) * 24.0f,
            ((first.z * cosine) * 24.0f + origin.z) + (second.z * sine) * 24.0f};
        auto* triangle = vertices.data() + 3 * (i - 1);
        triangle[0] = center;
        triangle[1].color = config.color.value & 0x00ffffffu;
        triangle[2].color = config.color.value & 0x00ffffffu;
        if (!project(projection, previous, triangle[1]) || !project(projection, next, triangle[2]))
            return;
        previous = next;
    }
    const std::uint32_t count = 48;
    auto* payload = queue.append(7, count * sizeof(Vertex) + 16);
    std::memcpy(payload, &count, 4);
    std::memcpy(payload + 4, vertices.data(), sizeof(vertices));
}
} // namespace skeet
