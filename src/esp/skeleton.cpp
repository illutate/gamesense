#include "skeet/esp/skeleton.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace skeet::esp {

void draw_skeleton(ui::RenderQueue& queue, const Projection& projection,
                   std::span<const SkeletonBone> bones, std::span<const Matrix3x4> transforms,
                   std::uint32_t color) {
    const auto count = std::min(bones.size(), transforms.size());
    if (count > 256)
        throw std::length_error("Skeleton exceeds native bone workspace");
    std::array<Point, 256> points;
    std::array<std::uint32_t, 8> projected{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& bone = bones[index];
        if (!(bone.flags & 0x100))
            continue;
        const auto& matrix = transforms[index];
        if (!project_point(projection, {matrix.rows[0][3], matrix.rows[1][3], matrix.rows[2][3]},
                           points[index]))
            continue;
        projected[index >> 5] |= std::uint32_t(1) << (index & 31);
        if (bone.parent == 0xFFFFFFFFu)
            continue;
        if (bone.parent >= points.size())
            throw std::out_of_range("Skeleton parent exceeds native bone workspace");
        if (!(projected[bone.parent >> 5] & (std::uint32_t(1) << (bone.parent & 31))))
            continue;
        const auto a = points[bone.parent], b = points[index];
        queue.line({a.x, a.y, b.x, b.y, color});
    }
}

} // namespace skeet::esp
