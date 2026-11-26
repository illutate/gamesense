#include "skeet/legit/accuracy_history.hpp"
#include "skeet/legit/studio.hpp"
#include "skeet/engine/opaque_native_word.hpp"
#include <cmath>
#include <cstring>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float add(float first, float second) {
    __asm__ __volatile__("addss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
float multiply(float first, float second) {
    __asm__ __volatile__("mulss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
float distance_squared(const Vector& first, const Vector& second) {
    const auto y = first[1] - second[1], x = first[0] - second[0], z = first[2] - second[2];
    return (y * y + x * x) + z * z;
}
Matrix3x4 angle_matrix(const Vector& angles, const Vector& origin,
                       const lagcomp::RotationMath& math) {
    constexpr float radians = 0.01745329238474369f;
    const auto sy = math.sine(angles[1] * radians), cy = math.cosine(angles[1] * radians);
    const auto sp = math.sine(angles[0] * radians), cp = math.cosine(angles[0] * radians);
    const auto sr = math.sine(angles[2] * radians), cr = math.cosine(angles[2] * radians);
    const auto srcy = sr * cy, srsy = sr * sy, crcy = cr * cy, crsy = cr * sy;
    return {{{cp * cy, srcy * sp - crsy, crcy * sp + srsy, origin[0]},
             {cp * sy, srsy * sp + crcy, crsy * sp - srcy, origin[1]},
             {-sp, sr * cp, cr * cp, origin[2]}}};
}
Matrix3x4 concatenate(const Matrix3x4& left, const Matrix3x4& right) {
    Matrix3x4 result;
    for (unsigned row = 0; row != 3; ++row)
        for (unsigned column = 0; column != 4; ++column)
            result.rows[row][column] =
                add(add(add(multiply(left.rows[row][2], right.rows[2][column]),
                            multiply(left.rows[row][1], right.rows[1][column])),
                        multiply(left.rows[row][0], right.rows[0][column])),
                    column == 3 ? left.rows[row][3] : 0.0f);
    return result;
}
Matrix3x4 rebase_matrix(const lagcomp::LagRecordHeader& old, const Vector& origin,
                        const Vector& angles, AccuracyMatrixScratch& scratch,
                        const lagcomp::RotationMath& math) {
    const auto old_rotation = angle_matrix({0.0f, old.animation_yaw, 0.0f}, {}, math);
    Matrix3x4 inverse;
    for (unsigned row = 0; row != 3; ++row)
        for (unsigned column = 0; column != 3; ++column)
            inverse.rows[row][column] = old_rotation.rows[column][row];
    inverse.rows[0][3] = multiply(add(add(multiply(old.origin[0], inverse.rows[0][0]),
                                          multiply(old.origin[1], inverse.rows[0][1])),
                                      multiply(old.origin[2], inverse.rows[0][2])),
                                  -1.0f);
    inverse.rows[1][3] = multiply(
        add(add(multiply(old.origin[0],
                         engine::read_opaque_native_float(scratch.inverse_row1.data())),
                multiply(old.origin[1],
                         engine::read_opaque_native_float(scratch.inverse_row1.data() + 1))),
            multiply(old.origin[2],
                     engine::read_opaque_native_float(scratch.inverse_row1.data() + 2))),
        -1.0f);
    inverse.rows[2][3] = multiply(
        add(add(multiply(old.origin[0],
                         engine::read_opaque_native_float(scratch.inverse_row2.data())),
                multiply(old.origin[1],
                         engine::read_opaque_native_float(scratch.inverse_row2.data() + 1))),
            multiply(old.origin[2],
                     engine::read_opaque_native_float(scratch.inverse_row2.data() + 2))),
        -1.0f);
    for (unsigned axis = 0; axis != 3; ++axis) {
        scratch.inverse_row1[axis] = inverse.rows[1][axis];
        scratch.inverse_row2[axis] = inverse.rows[2][axis];
    }
    return concatenate(angle_matrix(angles, origin, math), inverse);
}
}
std::int32_t select_accuracy_hitbox(const lagcomp::RecordRing& ring, std::int32_t requested,
                                    std::uint32_t hitbox, Vector& origin, Matrix3x4& matrix,
                                    float interval, AccuracyMatrixScratch& scratch,
                                    const lagcomp::RotationMath& math) {
    const lagcomp::LagRecord* previous = nullptr;
    for (std::uint32_t ordinal = 0; ordinal != ring.count; ++ordinal) {
        const auto& record = ring.at(ordinal);
        const auto& header = record.header;
        if (previous && distance_squared(previous->header.origin, header.origin) > 4096.0f)
            return -1;
        if (header.simulation_tick <= requested) {
            if (!(header.flags & 4u))
                return -1;
            if (!previous || requested <= header.simulation_tick ||
                previous->header.simulation_tick <= header.simulation_tick) {
                origin = header.origin;
                matrix = record.matrices[hitbox];
            } else {
                const auto old_time = float(header.simulation_tick) * interval;
                const auto fraction =
                    (float(requested) * interval - old_time) /
                    (float(previous->header.simulation_tick) * interval - old_time);
                Vector angles{0.0f, header.animation_yaw, 0.0f};
                if (previous->header.animation_yaw != header.animation_yaw) {
                    const auto first = lagcomp::history_angle_quaternion(angles, math);
                    const auto second = lagcomp::history_angle_quaternion(
                        {0.0f, previous->header.animation_yaw, 0.0f}, math);
                    angles = lagcomp::history_quaternion_angles(
                        lagcomp::slerp_history_quaternion(
                            first, lagcomp::align_history_quaternion(first, second), fraction,
                            math),
                        math);
                }
                for (unsigned axis = 0; axis != 3; ++axis)
                    origin[axis] =
                        (previous->header.origin[axis] - header.origin[axis]) * fraction +
                        header.origin[axis];
                matrix = concatenate(rebase_matrix(header, origin, angles, scratch, math),
                                     record.matrices[hitbox]);
            }
            return requested;
        }
        if (header.flags & 0x200u)
            return -1;
        previous = &record;
    }
    return -1;
}
void search_accuracy_history(const lagcomp::RecordRing& ring, const AccuracyHistoryRay& ray,
                             const Vector& newest, const AcquisitionHitbox& descriptor,
                             std::uint32_t hitbox, std::int32_t first, std::int32_t last,
                             std::int32_t& best_tick, float& best_cosine, float interval,
                             AccuracyMatrixScratch& scratch, const lagcomp::RotationMath& math) {
    const auto range_squared = ray.maximum_origin_distance * ray.maximum_origin_distance;
    const Vector center{(descriptor.maximum[0] + descriptor.minimum[0]) * 0.5f,
                        (descriptor.maximum[1] + descriptor.minimum[1]) * 0.5f,
                        (descriptor.maximum[2] + descriptor.minimum[2]) * 0.5f};
    for (auto tick = last; tick >= first; --tick) {
        Vector origin;
        Matrix3x4 matrix;
        if (select_accuracy_hitbox(ring, tick, hitbox, origin, matrix, interval, scratch, math) ==
            -1)
            continue;
        Vector point;
        for (unsigned row = 0; row != 3; ++row) {
            const auto& m = matrix.rows[row];
            point[row] = ((m[1] * center[1] + m[0] * center[0]) + m[2] * center[2]) + m[3];
        }
        if (4096.0f > range_squared && distance_squared(origin, newest) > range_squared)
            continue;
        const auto x = point[0] - ray.origin[0], y = point[1] - ray.origin[1],
                   z = point[2] - ray.origin[2];
        const auto inverse = 1.0f / (std::sqrt((x * x + y * y) + z * z) + 1.1920928955078125e-7f);
        const auto cosine = ((inverse * y) * ray.direction[1] + (inverse * x) * ray.direction[0]) +
                            (inverse * z) * ray.direction[2];
        if (cosine > best_cosine) {
            best_tick = tick;
            best_cosine = cosine;
        }
    }
}
void search_target_accuracy_history(const void* target, const StudioOffsets& offsets,
                                    std::ptrdiff_t origin_offset, const lagcomp::RecordRing& ring,
                                    const AccuracyHistoryRay& ray, std::uint32_t hitbox,
                                    std::int32_t first, std::int32_t last, std::int32_t& best_tick,
                                    float& best_cosine, float interval,
                                    AccuracyMatrixScratch& scratch,
                                    const lagcomp::RotationMath& math) {
    const auto set = load_target_hitbox_set(target, offsets);
    if (!set.studio || !set.count)
        return;
    const auto* box = set.descriptors + std::ptrdiff_t(hitbox) * 0x44;
    const AcquisitionHitbox descriptor{read<std::int32_t>(box, 0), read<std::int32_t>(box, 4),
                                       read<Vector>(box, 8), read<Vector>(box, 0x14),
                                       read<float>(box, 0x30)};
    const auto newest = ring.count ? ring.at(0).header.origin : read<Vector>(target, origin_offset);
    search_accuracy_history(ring, ray, newest, descriptor, hitbox, first, last, best_tick,
                            best_cosine, interval, scratch, math);
}
} // namespace skeet::legit
