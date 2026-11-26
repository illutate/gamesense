#include "skeet/lagcomp/interpolation.hpp"
#include "skeet/animation/bones.hpp"
#include <bit>
#include <cmath>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
constexpr float radians = 0.01745329238474369f;
constexpr float degrees = 57.2957763671875f;
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
Vector interpolated_angles(float first, float second, float fraction, const RotationMath& math) {
    if (first == second)
        return {0.0f, first, 0.0f};
    const auto a = history_angle_quaternion({0.0f, first, 0.0f}, math);
    const auto b = history_angle_quaternion({0.0f, second, 0.0f}, math);
    return history_quaternion_angles(
        slerp_history_quaternion(a, align_history_quaternion(a, b), fraction, math), math);
}
}
Quaternion history_angle_quaternion(const Vector& angles, const RotationMath& math) {
    const float yaw = (angles[1] * radians) * 0.5f;
    const float sy = math.sine(yaw), cy = math.cosine(yaw);
    const float pitch = (angles[0] * radians) * 0.5f;
    const float sp = math.sine(pitch), cp = math.cosine(pitch);
    const float roll = (angles[2] * radians) * 0.5f;
    const float sr = math.sine(roll), cr = math.cosine(roll);
    const float srcp = sr * cp, crsp = cr * sp, srsp = sr * sp, crcp = cr * cp;
    return {srcp * cy - crsp * sy, srcp * sy + crsp * cy, crcp * sy - srsp * cy,
            srsp * sy + crcp * cy};
}
Quaternion align_history_quaternion(const Quaternion& first, const Quaternion& second) noexcept {
    float difference = 0, sum = 0;
    for (unsigned i = 0; i != 4; ++i) {
        const auto minus = first[i] - second[i], plus = first[i] + second[i];
        difference += minus * minus;
        sum += plus * plus;
    }
    auto result = second;
    if (!(difference <= sum))
        for (auto& value : result)
            value = negate(value);
    return result;
}
Quaternion slerp_history_quaternion(const Quaternion& first, const Quaternion& second,
                                    float fraction, const RotationMath& math) {
    const float cosine = ((first[1] * second[1] + first[0] * second[0]) + first[2] * second[2]) +
                         first[3] * second[3];
    float inverse = 1.0f - fraction;
    Quaternion result;
    if (!(cosine + 1.0f > 0.000001f)) {
        result = {negate(second[1]), second[0], negate(second[3]), second[2]};
        inverse = math.sine(float(double(inverse) * 1.5707963267948966));
        fraction = math.sine(float(double(fraction) * 1.5707963267948966));
        for (unsigned i = 0; i != 3; ++i)
            result[i] = first[i] * inverse + result[i] * fraction;
    } else {
        if (1.0f - cosine > 0.000001f) {
            const float angle = math.arc_cosine(cosine);
            const float sine = math.sine(angle);
            inverse = math.sine(inverse * angle) / sine;
            fraction = math.sine(angle * fraction) / sine;
        }
        for (unsigned i = 0; i != 4; ++i)
            result[i] = second[i] * fraction + first[i] * inverse;
    }
    return result;
}
Vector history_quaternion_angles(const Quaternion& quaternion, const RotationMath& math) {
    const auto matrix = animation::bone_local_matrix({}, quaternion);
    const float horizontal =
        std::sqrt(matrix.rows[0][0] * matrix.rows[0][0] + matrix.rows[1][0] * matrix.rows[1][0]);
    Vector result;
    if (horizontal > 0.001f) {
        result[1] = float(math.arc_tangent2(double(matrix.rows[1][0]), double(matrix.rows[0][0]))) *
                    degrees;
        result[0] =
            float(math.arc_tangent2(double(negate(matrix.rows[2][0])), double(horizontal))) *
            degrees;
        result[2] = float(math.arc_tangent2(double(matrix.rows[2][1]), double(matrix.rows[2][2]))) *
                    degrees;
    } else {
        result[1] =
            float(math.arc_tangent2(double(negate(matrix.rows[0][1])), double(matrix.rows[1][1]))) *
            degrees;
        result[0] =
            float(math.arc_tangent2(double(negate(matrix.rows[2][0])), double(horizontal))) *
            degrees;
        result[2] = 0.0f;
    }
    return result;
}
const LagRecord* history_interpolation_source(const RecordRing& ring, std::uint32_t ordinal,
                                              const LagRecord& selected,
                                              std::int32_t requested_tick,
                                              std::int32_t planned_ticks,
                                              const HistoryTickWindow& window) noexcept {
    if (requested_tick != selected.header.simulation_tick || planned_ticks ||
        requested_tick <= window.last || window.last < window.corrected ||
        ordinal + 1u >= ring.count)
        return nullptr;
    const auto& older = ring.at(ordinal + 1u);
    if (older.header.simulation_tick >= window.last)
        return nullptr;
    const auto& a = selected.header.origin;
    const auto& b = older.header.origin;
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    if ((y * y + x * x) + z * z > 4096.0f)
        return nullptr;
    return &older;
}
float interpolate_history_fields(LagRecord& destination, const LagRecord& newer,
                                 const LagRecord& older, float requested_time, float interval,
                                 const RotationMath& math, Vector* interpolated_rotation) {
    destination.header.simulation_tick = time_to_tick(requested_time, interval);
    const float old_time = float(older.header.simulation_tick) * interval;
    const float difference = float(newer.header.simulation_tick) * interval - old_time;
    const float fraction = _mm_cvtss_f32(_mm_min_ss(
        _mm_max_ss(_mm_set_ss((requested_time - old_time) / difference), _mm_setzero_ps()),
        _mm_set_ss(1.0f)));
    const auto angles =
        interpolated_angles(older.header.animation_yaw, newer.header.animation_yaw, fraction, math);
    if (interpolated_rotation)
        *interpolated_rotation = angles;
    destination.header.animation_yaw = angles[1];
    destination.header.player_flags = newer.header.player_flags;
    for (unsigned i = 0; i != 3; ++i) {
        destination.header.origin[i] =
            (newer.header.origin[i] - older.header.origin[i]) * fraction + older.header.origin[i];
        destination.header.velocity[i] =
            (newer.header.velocity[i] - older.header.velocity[i]) * fraction +
            older.header.velocity[i];
    }
    return fraction;
}
} // namespace skeet::lagcomp
