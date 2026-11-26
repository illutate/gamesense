#include "skeet/rage_hitboxes.hpp"
#include <algorithm>

namespace skeet {
namespace {
Matrix3x4 hitbox_rotation(const RageVector& rotation, const RageTrigonometry& trig) {
    constexpr float radians = 0.01745329238474369f;
    const float sy = trig.sine(rotation[1] * radians);
    const float cy = trig.cosine(rotation[1] * radians);
    const float sp = trig.sine(rotation[0] * radians);
    const float cp = trig.cosine(rotation[0] * radians);
    const float sr = trig.sine(rotation[2] * radians);
    const float cr = trig.cosine(rotation[2] * radians);
    const float crcy = cr * cy;
    const float crsy = cr * sy;
    const float srcy = sr * cy;
    const float srsy = sr * sy;
    return {{{cp * cy, srcy * sp - crsy, crcy * sp + srsy, 0.0f},
             {cp * sy, srsy * sp + crcy, crsy * sp - srcy, 0.0f},
             {-sp, sr * cp, cr * cp, 0.0f}}};
}
Matrix3x4 compose_hitbox_matrix(const Matrix3x4& bone, const Matrix3x4& rotation) noexcept {
    Matrix3x4 result;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            const float combined = (bone.rows[row][2] * rotation.rows[2][column] +
                                    bone.rows[row][1] * rotation.rows[1][column]) +
                                   bone.rows[row][0] * rotation.rows[0][column];
            result.rows[row][column] = combined + (column == 3 ? bone.rows[row][3] : 0.0f);
        }
    }
    return result;
}
}

RageScanHitbox prepare_rage_scan_hitbox(const RageLocalHitbox& hitbox, const Matrix3x4& matrix_in,
                                        const RageTrigonometry& trig) noexcept {
    const auto matrix = compose_hitbox_matrix(matrix_in, hitbox_rotation(hitbox.rotation, trig));
    RageCapsule capsule{{}, {}, hitbox.radius};
    for (unsigned row = 0; row < 3; ++row) {
        const auto& m = matrix.rows[row];
        capsule.first[row] =
            ((hitbox.minimum[1] * m[1] + m[0] * hitbox.minimum[0]) + m[2] * hitbox.minimum[2]) +
            m[3];
        const float pair = row == 2 ? m[1] * hitbox.maximum[1] + m[0] * hitbox.maximum[0]
                                    : hitbox.maximum[0] * m[0] + hitbox.maximum[1] * m[1];
        capsule.second[row] = (pair + m[2] * hitbox.maximum[2]) + m[3];
    }
    return {hitbox.hitgroup, capsule, hitbox.engine_hitbox, matrix};
}
std::vector<RageScanHitbox> prepare_rage_scan_hitboxes(std::span<const RageLocalHitbox> local,
                                                       const HitboxMatrices& matrices,
                                                       const RageTrigonometry& trig) {
    std::vector<RageScanHitbox> result;
    const auto count = std::min(local.size(), matrices.size());
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        result.push_back(prepare_rage_scan_hitbox(local[index], matrices[index], trig));
    return result;
}
} // namespace skeet
