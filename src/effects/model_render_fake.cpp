#include "skeet/effects/model_render.hpp"
#include <bit>
#include <cmath>

namespace skeet::effects {
namespace {
float normalize(float angle) {
    if (std::isnan(angle))
        return 0.0f;
    if (std::fabs(angle) > 180.0f) {
        const auto quotient = std::fabs(angle / 360.0f);
        auto turns = float(std::ceil(double(quotient)));
        if (turns - quotient > 0.5f)
            turns -= 1.0f;
        const auto correction = turns * 360.0f;
        angle = angle < 0.0f ? angle + correction : angle - correction;
    }
    return angle;
}
float negate(float f) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(f) ^ 0x80000000u);
}
Matrix3x4 rotation(float sy, float cy, float sp, float cp, float sr, float cr) {
    return {{{cp * cy, (sr * cy) * sp - cr * sy, (cr * cy) * sp + sr * sy, 0.0f},
             {cp * sy, (sr * sy) * sp + cr * cy, (cr * sy) * sp - sr * cy, 0.0f},
             {negate(sp), sr * cp, cr * cp, 0.0f}}};
}
Matrix3x4 concatenate(const Matrix3x4& left, const Matrix3x4& right) {
    Matrix3x4 result;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned col = 0; col < 4; ++col)
            result.rows[r][col] =
                ((left.rows[r][2] * right.rows[2][col] + left.rows[r][1] * right.rows[1][col]) +
                 left.rows[r][0] * right.rows[0][col]) +
                (col == 3 ? left.rows[r][3] : 0.0f);
    return result;
}
}
bool build_fake_model_matrices(const FakeModelPose& pose, const RageVector& origin,
                               std::span<Matrix3x4> output, const RageTrigonometry& trig) {
    const auto delta = std::bit_cast<std::int32_t>(std::uint32_t(pose.capture_tick) -
                                                   std::uint32_t(pose.current_tick));
    const auto sign = delta < 0 ? UINT32_MAX : 0u;
    const auto absolute = std::bit_cast<std::int32_t>((std::uint32_t(delta) ^ sign) - sign);
    if (!pose.available || pose.bones.empty() || absolute > 16)
        return false;
    if (pose.blend <= 0.0f) {
        auto yaw = pose.observed_yaw - pose.reference_yaw;
        // Unlike the vector normalizer, this caller retains a NaN yaw.
        if (!std::isnan(yaw))
            yaw = normalize(yaw);
        if (!(std::fabs(yaw) >= 5.0f)) {
            RageVector angles;
            for (unsigned n = 0; n < 3; ++n)
                angles[n] = normalize(pose.observed_angles[n] - pose.reference_angles[n]);
            if ((angles[0] * angles[0] + angles[1] * angles[1]) + angles[2] * angles[2] < 25.0f)
                return false;
        }
    }
    if (!(pose.blend > 0.0f)) {
        for (std::size_t n = 0; n < pose.bones.size(); ++n) {
            output[n] = pose.bones[n];
            for (unsigned r = 0; r < 3; ++r)
                output[n].rows[r][3] = output[n].rows[r][3] + origin[r];
        }
        return true;
    }
    constexpr float rad = 0.01745329238474369f;
    const auto yaw = pose.transform_angles[1] * rad, pitch = pose.transform_angles[0] * rad,
               roll = pose.transform_angles[2] * rad;
    const auto sy = trig.sine(yaw), cy = trig.cosine(yaw), sp = trig.sine(pitch),
               cp = trig.cosine(pitch), sr = trig.sine(roll), cr = trig.cosine(roll);
    const auto forward = rotation(sy, cy, sp, cp, sr, cr);
    Matrix3x4 inverse;
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned col = 0; col < 3; ++col)
            inverse.rows[r][col] = forward.rows[col][r];
        inverse.rows[r][3] = negate((inverse.rows[r][0] * 0.0f + inverse.rows[r][1] * 0.0f) +
                                    inverse.rows[r][2] * 0.0f);
    }
    const auto sy2 = trig.sine(pose.rotation_radians), cy2 = trig.cosine(pose.rotation_radians);
    const auto sp2 = trig.sine(0.0f), cp2 = trig.cosine(0.0f), sr2 = trig.sine(0.0f),
               cr2 = trig.cosine(0.0f);
    auto rotated = rotation(sy2, cy2, sp2, cp2, sr2, cr2);
    rotated.rows[0][3] = origin[0];
    rotated.rows[1][3] = origin[1];
    rotated.rows[2][3] = origin[2];
    const auto transform = concatenate(rotated, inverse);
    for (std::size_t n = 0; n < pose.bones.size(); ++n)
        output[n] = concatenate(transform, pose.bones[n]);
    return true;
}
} // namespace skeet::effects
