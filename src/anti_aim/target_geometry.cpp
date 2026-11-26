#include "skeet/anti_aim/target_geometry.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skeet::anti_aim {
namespace {
constexpr std::array<std::uint32_t, 5> selected_hitboxes{0, 3, 6, 11, 12}; // 43474A30
constexpr float epsilon = 1.1920928955078125e-7f;
float sign_flip(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float length(const RageVector& v) noexcept {
    return std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
}
RageVector normalize(const RageVector& v) noexcept {
    const auto inverse = 1.0f / (length(v) + epsilon);
    return {inverse * v[0], inverse * v[1], inverse * v[2]};
}
Matrix3x4 rotated_matrix(const Matrix3x4& bone, const RageVector& angles,
                         const RageTrigonometry& trig) {
    constexpr float radians = 0.01745329238474369f;
    const auto sy = trig.sine(angles[1] * radians), cy = trig.cosine(angles[1] * radians);
    const auto sp = trig.sine(angles[0] * radians), cp = trig.cosine(angles[0] * radians);
    const auto sr = trig.sine(angles[2] * radians), cr = trig.cosine(angles[2] * radians);
    const auto crcy = cr * cy, crsy = cr * sy, srcy = sr * cy, srsy = sr * sy;
    const Matrix3x4 rotation{{{cp * cy, srcy * sp - crsy, crcy * sp + srsy, 0.0f},
                              {cp * sy, srsy * sp + crcy, crsy * sp - srcy, 0.0f},
                              {sign_flip(sp), sr * cp, cr * cp, 0.0f}}};
    Matrix3x4 output;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            const auto combined = (bone.rows[row][2] * rotation.rows[2][column] +
                                   bone.rows[row][1] * rotation.rows[1][column]) +
                                  bone.rows[row][0] * rotation.rows[0][column];
            output.rows[row][column] = combined + (column == 3 ? bone.rows[row][3] : 0.0f);
        }
    return output;
}
RageVector transform(const Matrix3x4& matrix, const RageVector& point) noexcept {
    RageVector result;
    for (unsigned row = 0; row < 3; ++row) {
        const auto& m = matrix.rows[row];
        result[row] = ((point[0] * m[0] + point[1] * m[1]) + point[2] * m[2]) + m[3];
    }
    return result;
}
RageVector capsule_support(const RageVector& first, const RageVector& second,
                           const RageVector& direction, float radius) noexcept {
    const auto projection =
        ((second[0] - first[0]) * direction[0] + (second[1] - first[1]) * direction[1]) +
        (second[2] - first[2]) * direction[2];
    const auto& endpoint = projection < 0.0f ? first : second;
    const auto scale = radius / length(direction);
    return {direction[0] * scale + endpoint[0], direction[1] * scale + endpoint[1],
            direction[2] * scale + endpoint[2]};
}
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
}

TargetPoints build_target_points(const RageVector& target_origin, float view_offset_z,
                                 const RageVector& shoot_origin,
                                 std::span<const RageLocalHitbox> hitboxes,
                                 const HitboxMatrices& matrices,
                                 const RageTrigonometry& trig) { // 433F2CC1
    TargetPoints output;
    const RageVector center{target_origin[0], target_origin[1],
                            (view_offset_z - 2.0f) + target_origin[2]};
    output.points.fill(center);
    if (hitboxes.empty())
        return output;
    if (!trig.sine || !trig.cosine)
        throw std::logic_error("Target geometry requires native trigonometry bindings");
    const auto forward = normalize(
        {center[0] - shoot_origin[0], center[1] - shoot_origin[1], center[2] - shoot_origin[2]});
    RageVector right, up;
    if (forward[0] == 0.0f && forward[1] == 0.0f) {
        right = {0.0f, -1.0f, 0.0f};
        up = {sign_flip(forward[2]), 0.0f, 0.0f};
    } else {
        right = normalize({forward[1] - forward[2] * 0.0f, forward[2] * 0.0f - forward[0],
                           forward[0] * 0.0f - forward[1] * 0.0f});
        up = normalize({right[1] * forward[2] - right[2] * forward[1],
                        right[2] * forward[0] - right[0] * forward[2],
                        right[0] * forward[1] - right[1] * forward[0]});
    }
    float minimum = 1.0f, maximum = -1.0f;
    std::uint32_t band = 0;
    for (const auto index : selected_hitboxes) {
        if (index >= hitboxes.size())
            continue;
        const auto& hitbox = hitboxes[index];
        const auto matrix = rotated_matrix(matrices[index], hitbox.rotation, trig);
        std::array<RageVector, 4> candidates;
        std::uint32_t count;
        auto group = hitbox.hitgroup;
        if (hitbox.radius > 0.0f) {
            const auto first = transform(matrix, hitbox.minimum),
                       second = transform(matrix, hitbox.maximum);
            candidates[0] = capsule_support(first, second, right, hitbox.radius);
            candidates[1] = capsule_support(
                first, second, {sign_flip(right[0]), sign_flip(right[1]), sign_flip(right[2])},
                hitbox.radius);
            count = 2;
            if (index == 0 || index == 6) {
                const auto slot = index == 0 ? 4 : 5;
                output.points[slot] = capsule_support(first, second, up, hitbox.radius);
                output.hitgroups[slot] = group = 3;
            }
        } else {
            if (index == 11) {
                minimum = 1.0f;
                maximum = -1.0f;
            }
            // The native pair stays2/3 after any box. Only hitbox11 resets its scores.
            band = 2;
            count = 4;
            // This is the max-Y face, with coordinates halved before transformation.
            const std::array<RageVector, 4> local{
                {{hitbox.minimum[0], hitbox.maximum[1], hitbox.minimum[2]},
                 {hitbox.maximum[0], hitbox.maximum[1], hitbox.maximum[2]},
                 {hitbox.maximum[0], hitbox.maximum[1], hitbox.minimum[2]},
                 {hitbox.minimum[0], hitbox.maximum[1], hitbox.maximum[2]}}};
            for (unsigned i = 0; i < count; ++i)
                candidates[i] =
                    transform(matrix, {local[i][0] * 0.5f, local[i][1] * 0.5f, local[i][2] * 0.5f});
        }
        if (!group)
            continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& point = candidates[i];
            const auto projection =
                ((point[1] - center[1]) * right[1] + (point[0] - center[0]) * right[0]) +
                (point[2] - point[2]) * right[2];
            const RageVector delta{(right[0] * projection + center[0]) - shoot_origin[0],
                                   (right[1] * projection + center[1]) - shoot_origin[1],
                                   (right[2] * projection + point[2]) - shoot_origin[2]};
            const auto inverse = 1.0f / (length(delta) + epsilon);
            const auto score = ((delta[1] * inverse) * right[1] + (delta[0] * inverse) * right[0]) +
                               (inverse * delta[2]) * right[2];
            if (score < minimum) {
                output.points[band] = point;
                output.hitgroups[band] = group;
                minimum = score;
            }
            if (score > maximum) {
                output.points[band + 1] = point;
                output.hitgroups[band + 1] = group;
                maximum = score;
            }
        }
    }
    return output;
}

TargetPoints target_points_from_entity(void* entity, const TargetGeometryOffsets& offsets,
                                       const RageVector& target_origin,
                                       const RageVector& shoot_origin,
                                       const HitboxMatrices& matrices,
                                       const RageTrigonometry& trig) {
    const auto* view = engine::invoke<const RageVector*>(entity, 77);
    const auto height = (*view)[2];
    const auto* handle = field<const void*>(entity, offsets.studio_header);
    const auto* header = handle ? field<const void*>(handle, 0) : nullptr;
    if (!header)
        return build_target_points(target_origin, height, shoot_origin, {}, matrices, trig);
    const auto index = field<std::int32_t>(entity, offsets.hitbox_set);
    const auto* set = static_cast<const std::byte*>(header) + field<std::int32_t>(header, 0xb0) +
                      std::ptrdiff_t(index) * 12;
    const auto count = field<std::int32_t>(set, 4);
    if (count <= 0)
        return build_target_points(target_origin, height, shoot_origin, {}, matrices, trig);
    const auto* hitboxes = set + field<std::int32_t>(set, 8);
    std::array<RageLocalHitbox, 13> selected{};
    for (const auto hitbox_index : selected_hitboxes) {
        if (hitbox_index >= std::uint32_t(count))
            continue;
        const auto raw =
            field<TargetStudioHitbox>(hitboxes, hitbox_index * sizeof(TargetStudioHitbox));
        selected[hitbox_index] = {
            raw.hitgroup, raw.minimum, raw.maximum,
            raw.rotation, raw.radius,  hitboxes + hitbox_index * sizeof(TargetStudioHitbox)};
    }
    return build_target_points(
        target_origin, height, shoot_origin,
        std::span(selected.data(), std::min<std::size_t>(std::size_t(count), selected.size())),
        matrices, trig);
}
} // namespace skeet::anti_aim
