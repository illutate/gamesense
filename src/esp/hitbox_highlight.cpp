#include "skeet/esp/hitbox_highlight.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::esp {
namespace {
struct Vertex {
    float x, y;
    std::uint32_t color;
};
float length(const RageVector& value) noexcept {
    return std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]);
}
RageVector normalize(const RageVector& value) noexcept {
    const float inverse = 1.0f / (length(value) + std::numeric_limits<float>::epsilon());
    return {value[0] * inverse, value[1] * inverse, value[2] * inverse};
}
bool project(const Projection& projection, const RageVector& position, bool support,
             Vertex& result) noexcept {
    const auto& m = projection.matrix;
    const float w = (support ? (position[1] * m[13] + position[0] * m[12])
                             : (position[0] * m[12] + position[1] * m[13])) +
                    position[2] * m[14] + m[15];
    if (w < 0.001f)
        return false;
    const float inverse = 1.0f / w;
    const float horizontal = (support ? (position[1] * m[1] + position[0] * m[0])
                                      : (position[0] * m[0] + position[1] * m[1])) +
                             position[2] * m[2] + m[3];
    const float vertical = (support ? (position[1] * m[5] + position[0] * m[4])
                                    : (position[0] * m[4] + position[1] * m[5])) +
                           position[2] * m[6] + m[7];
    result.x = (horizontal * inverse) * projection.horizontal_scale + projection.horizontal_scale;
    result.y = projection.vertical_scale - (vertical * inverse) * projection.vertical_scale;
    return true;
}
}
void draw_capsule_highlight(ui::RenderQueue& queue, const Projection& projection,
                            const RageVector& origin, const RageCapsule& capsule,
                            std::uint32_t color, const RageTrigonometry& trig) {
    const RageVector center{(capsule.first[0] + capsule.second[0]) * 0.5f,
                            (capsule.second[1] + capsule.first[1]) * 0.5f,
                            (capsule.second[2] + capsule.first[2]) * 0.5f};
    const auto axis =
        normalize({center[0] - origin[0], center[1] - origin[1], center[2] - origin[2]});
    const float sine = trig.sine(0.5235987901687622f), cosine = trig.cosine(0.5235987901687622f);
    const float complement = 1.0f - cosine;
    const float xy = (axis[1] * axis[0]) * complement;
    const float xz = (axis[0] * axis[2]) * complement;
    const float yz = (axis[1] * axis[2]) * complement;
    Vertex projected_center{0, 0, color};
    if (!project(projection, center, false, projected_center))
        return;
    RageVector side{0.0f, -1.0f, 0.0f};
    if (!(axis[0] == 0.0f && axis[1] == 0.0f))
        side = normalize(
            {axis[1] - axis[2] * 0.0f, axis[2] * 0.0f - axis[0], axis[0] * 0.0f - axis[1] * 0.0f});
    std::array<Vertex, 12> perimeter;
    for (auto& point : perimeter) {
        const RageVector segment{capsule.second[0] - capsule.first[0],
                                 capsule.second[1] - capsule.first[1],
                                 capsule.second[2] - capsule.first[2]};
        const float dot = (segment[1] * side[1] + segment[0] * side[0]) + segment[2] * side[2];
        const auto& endpoint = dot >= 0.0f ? capsule.second : capsule.first;
        const float radius = capsule.radius / length(side);
        const RageVector support{side[0] * radius + endpoint[0], side[1] * radius + endpoint[1],
                                 side[2] * radius + endpoint[2]};
        point.color = color & 0xffffff;
        if (!project(projection, support, true, point))
            return;
        const float x = side[0], y = side[1], z = side[2];
        side[0] = ((xy - axis[2] * sine) * y +
                   ((1.0f - axis[0] * axis[0]) * cosine + axis[0] * axis[0]) * x) +
                  (axis[1] * sine + xz) * z;
        side[2] = z * ((1.0f - axis[2] * axis[2]) * cosine + axis[2] * axis[2]) +
                  ((xz - axis[1] * sine) * x + (axis[0] * sine + yz) * y);
        side[1] = ((axis[2] * sine + xy) * x +
                   ((1.0f - axis[1] * axis[1]) * cosine + axis[1] * axis[1]) * y) +
                  (yz - axis[0] * sine) * z;
    }
    std::array<Vertex, 36> vertices;
    for (unsigned i = 0; i < 12; ++i) {
        vertices[i * 3] = perimeter[i];
        vertices[i * 3 + 1] = projected_center;
        vertices[i * 3 + 2] = perimeter[(i + 1) % 12];
    }
    const std::uint32_t count = 36;
    auto* payload = queue.append(7, count * sizeof(Vertex) + 16);
    std::memcpy(payload, &count, 4);
    std::memcpy(payload + 4, vertices.data(), sizeof(vertices));
}
void draw_hitbox_highlight(ui::RenderQueue& queue, const AimbotVisualizationConfig& config,
                           HitboxHighlightState& state, const RuntimePlayer& local,
                           const HitboxHighlightContext& context) {
    if (!context.active)
        return;
    if (local.life_state) {
        state.target_handle = -1;
        return;
    }
    if (!config.enabled.value && !config.safe_point.value)
        return;
    if (state.target_handle == -1)
        return;
    const auto target = context.by_handle(context.engine_context, state.target_handle);
    if (!target.player ||
        !rage::target_is_eligible(context.targets, target.player->engine_entity) ||
        target.player->move_type == 8 || target.player->move_type == 10 ||
        (target.movement_flags & 0x40)) {
        state.target_handle = -1;
        return;
    }
    if (!target.model || !target.model->studio_header || target.hitboxes.empty())
        return;
    for (const auto& entry : state.entries) {
        if (!entry.flags)
            continue;
        if (entry.hitbox >= std::int32_t(target.hitboxes.size()))
            return;
        const auto& hitbox = target.hitboxes[std::size_t(entry.hitbox)];
        if (!(hitbox.radius > 0.0f))
            return;
        HitboxMatrices matrices{};
        matrices[0] = target.bones[target.model->hitbox_bones[std::size_t(entry.hitbox)]];
        const auto transformed =
            prepare_rage_scan_hitboxes({&hitbox, 1}, matrices, context.trigonometry);
        draw_capsule_highlight(queue, context.projection, context.view_origin,
                               transformed[0].capsule,
                               entry.flags & 2 ? config.safe_point_color.value : config.color.value,
                               context.trigonometry);
    }
}
} // namespace skeet::esp
