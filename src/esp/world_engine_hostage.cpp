#include "skeet/esp/world_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include "world_engine_detail.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
using namespace world_detail;
namespace {
Vector3 transform(const Matrix3x4& matrix, Vector3 value, bool second) {
    return {((value.x * matrix.rows[0][0] + value.y * matrix.rows[0][1]) +
             value.z * matrix.rows[0][2]) +
                matrix.rows[0][3],
            ((value.y * matrix.rows[1][1] + value.x * matrix.rows[1][0]) +
             value.z * matrix.rows[1][2]) +
                matrix.rows[1][3],
            second ? ((value.z * matrix.rows[2][2] + value.x * matrix.rows[2][0]) +
                      value.y * matrix.rows[2][1]) +
                         matrix.rows[2][3]
                   : ((value.x * matrix.rows[2][0] + value.y * matrix.rows[2][1]) +
                      value.z * matrix.rows[2][2]) +
                         matrix.rows[2][3]};
}
}
void WorldEntityEngine::hostage(ui::RenderQueue& queue, const EspFrameContext& frame,
                                RuntimePlayer& view, void* entity) {
    auto* const viewer = view.engine_entity;
    if (!frame.world_config.hostages.enabled.value)
        return;
    if (dormant(entity))
        return;
    const auto& offsets = bindings_.offsets;
    if ((field<std::uint8_t>(entity, offsets.hostage_effects) & 2) ||
        field<std::int32_t>(entity, offsets.hostage_life_state) >= 2)
        return;
    std::array<Matrix3x4, 256> matrices;
    // Genuine IClientRenderable::SetupBones, not the player's client bone
    // builder. It may replace the studio wrapper and selected hitbox set.
    if (!engine::invoke<bool>(static_cast<std::byte*>(entity) + 4, 13, matrices.data(),
                              std::int32_t(256), std::int32_t(0x300), bindings_.current_time))
        return;
    const auto* wrapper = field<const void*>(entity, offsets.studio_wrapper);
    if (!wrapper)
        return;
    const auto* header = field<const std::byte*>(wrapper, 0);
    if (!header)
        return;
    const auto set_index = field<std::int32_t>(entity, offsets.hitbox_set);
    const auto* set = header + field<std::int32_t>(header, 0xB0) + set_index * 12;
    const auto count = field<std::int32_t>(set, 4);
    if (count == 0)
        return;
    const auto* hitbox = set + field<std::int32_t>(set, 8);
    std::int32_t left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (std::int32_t n = 0; n < count; ++n, hitbox += 68) {
        // Native copies all68 bytes before transforming this hitbox.
        std::array<std::byte, 68> copy;
        std::memcpy(copy.data(), hitbox, copy.size());
        const auto bone = field<std::uint32_t>(copy.data(), 0);
        if (bone >= matrices.size())
            throw std::out_of_range("Hostage hitbox exceeds the native256-matrix workspace");
        const auto first = transform(matrices[bone], field<Vector3>(copy.data(), 8), false);
        const auto second = transform(matrices[bone], field<Vector3>(copy.data(), 20), true);
        for (const auto point : std::array<Vector3, 8>{{{first.x, first.y, first.z},
                                                        {first.x, first.y, second.z},
                                                        {first.x, second.y, first.z},
                                                        {first.x, second.y, second.z},
                                                        {second.x, first.y, first.z},
                                                        {second.x, first.y, second.z},
                                                        {second.x, second.y, first.z},
                                                        {second.x, second.y, second.z}}}) {
            Point screen;
            if (!project_point(frame.draw.projection, point, screen))
                return;
            left = std::min(left, screen.x);
            right = std::max(right, screen.x);
            top = std::min(top, screen.y);
            bottom = std::max(bottom, screen.y);
        }
    }
    left = std::max(left, 0);
    right = std::max(right, 0);
    top = std::max(top, 0);
    bottom = std::max(bottom, 0);
    if (left >= right || top >= bottom)
        return;
    const auto width = detail::sub(right, left), height = detail::sub(bottom, top);
    const auto center = detail::add(left, width / 2);
    queue.text(center, detail::sub(top, 5), frame.world_config.hostages.color.value, 0x14, 0,
               u"HOSTAGE");
    auto baseline = detail::add(top, 3);
    if (frame.player_config.bounding_box.value) {
        queue.rectangle_outline({detail::sub(left, 1), detail::sub(top, 1), detail::add(width, 2),
                                 detail::add(height, 2), 0x8C000000u},
                                1);
        queue.rectangle_outline({left, top, width, height, frame.world_config.hostages.color.value},
                                1);
        queue.rectangle_outline({detail::add(left, 1), detail::add(top, 1), detail::sub(width, 2),
                                 detail::sub(height, 2), 0x8C000000u},
                                1);
        baseline = detail::add(bottom, 6);
    }
    if (frame.player_config.distance.value)
        distance(
            queue, {center, baseline},
            squared(field<Vector3>(viewer, offsets.origin), field<Vector3>(entity, offsets.origin)),
            0xC8FFFFFFu);
}
} // namespace skeet::esp
