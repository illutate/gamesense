#include "action_detail.hpp"
#include "skeet/rage_safe_points.hpp"
#include "../rage_point_math.hpp"
#include <bit>

namespace skeet::rage::action_detail {
std::vector<Point> points(const client::CommandFrame& frame,
                          const AcquisitionTargetSettings& policy, const lagcomp::LagRecord& record,
                          const scan_detail::Model& model, const scan_detail::Geometry& geometry,
                          std::uint32_t index, bool safe, bool regular,
                          const ActionStateBindings& bindings) {
    const RagePointGeneration generation{frame.origin_dc,
                                         0xFFFFFFFFU,
                                         policy.words[4],
                                         std::bit_cast<float>(policy.words[3]),
                                         std::bit_cast<float>(frame.field_160),
                                         bindings.preamble.weapon_settings.minimum_hit_chance.value,
                                         (bindings.state.flags & 4U) != 0};
    std::vector<Point> result;
    result.reserve(16);
    if (safe) {
        const auto center = rage_point_detail::midpoint(geometry.primary[index].capsule);
        std::array<Matrix3x4, 3> matrices;
        for (std::size_t i = 0; i != 3; ++i)
            matrices[i] = record.alternatives[i].matrices[index];
        std::vector<RageAimPoint> descriptors;
        if (index == 0) {
            const RageHeadOcclusion occlusion{model.hitboxes[6],
                                              {record.alternatives[0].matrices[6],
                                               record.alternatives[1].matrices[6],
                                               record.alternatives[2].matrices[6]}};
            descriptors = generate_rage_head_safe_points(generation, center, record.header.origin,
                                                         model.hitboxes[0], matrices, &occlusion,
                                                         bindings.scan.trigonometry);
        } else {
            descriptors = generate_rage_body_safe_points(generation, center, record.header.origin,
                                                         index, model.hitboxes[index], matrices);
        }
        for (const auto& point : descriptors)
            result.push_back({point.position, point.flags, 2});
    }
    if (regular) {
        const auto descriptors = generate_rage_regular_hitbox_points(
            generation, index, model.hitboxes, geometry.primary);
        for (const auto& point : descriptors)
            result.push_back({point.position, point.flags, 1});
    }
    return result;
}
} // namespace skeet::rage::action_detail
