#include "skeet/legit/historical_bones.hpp"
#include "skeet/legit/runtime.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <immintrin.h>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
}
void refresh_hitbox_attachments(void* entity, const animation::BoneModel& model,
                                std::span<Matrix3x4, 19> hitboxes,
                                std::span<Matrix3x4, 256> scratch, std::ptrdiff_t accessor,
                                AttachmentUpdate update) {
    if (model.studio_header)
        for (std::size_t i = 0; i < std::min(std::size_t(19), model.hitbox_bones.size()); ++i)
            scratch[model.hitbox_bones[i]] = hitboxes[i];
    if (update) {
        const auto readable = read<std::uint32_t>(entity, accessor + 8),
                   writable = read<std::uint32_t>(entity, accessor + 12);
        write(entity, accessor + 8, 0xFFF00u);
        write(entity, accessor + 12, 0xFFF00u);
        update(entity, scratch.data(), 0x300);
        write(entity, accessor + 12, writable);
        write(entity, accessor + 8, readable);
    }
    animation::select_hitbox_bone_matrices(model, scratch, hitboxes);
}
std::int32_t historical_target_bones(const lagcomp::RecordRing& ring, void* entity,
                                     std::int32_t requested, std::span<Matrix3x4, 19> output,
                                     std::span<Matrix3x4, 256> scratch,
                                     const HistoricalBoneBindings& bindings) {
    const lagcomp::LagRecord *previous = nullptr, *selected = nullptr;
    for (std::uint32_t ordinal = 0; ordinal < ring.count; ++ordinal) {
        const auto& record = ring.at(ordinal);
        if (previous) {
            const auto& a = previous->header.origin;
            const auto& b = record.header.origin;
            const auto y = a[1] - b[1], x = a[0] - b[0], z = a[2] - b[2];
            if ((y * y + x * x) + z * z > 4096.0f)
                return -1;
        }
        if (record.header.simulation_tick <= requested) {
            selected = &record;
            break;
        }
        if (record.header.flags & 0x200u)
            return -1;
        previous = &record;
    }
    if (!selected || !(selected->header.flags & 4u))
        return -1;
    std::copy(selected->matrices.begin(), selected->matrices.end(), output.begin());
    auto result = requested;
    if (!previous || requested <= selected->header.simulation_tick ||
        previous->header.simulation_tick <= selected->header.simulation_tick)
        result = selected->header.simulation_tick;
    else {
        const auto old_time = float(selected->header.simulation_tick) * bindings.interval;
        const auto fraction =
            (float(requested) * bindings.interval - old_time) /
            (float(previous->header.simulation_tick) * bindings.interval - old_time);
        for (auto& matrix : output)
            for (unsigned axis = 0; axis != 3; ++axis)
                matrix.rows[axis][3] +=
                    (previous->header.origin[axis] - selected->header.origin[axis]) * fraction;
    }
    const auto& o = bindings.offsets;
    const auto& snapshot = bindings.snapshot;
    const auto old_432 = read<std::uint32_t>(entity, o.common.field_432b1f5b);
    write(entity, o.common.field_432b1f5b, snapshot.field_12c);
    const auto old_0d = read<std::uint32_t>(entity, o.common.field_0d560fac);
    write(entity, o.common.field_0d560fac, snapshot.field_148);
    const auto old_e23 = read<std::array<std::uint32_t, 2>>(entity, o.field_e23a9be1);
    write(entity, o.field_e23a9be1, snapshot.field_1a8);
    if (selected->header.collision_max_z != snapshot.field_130) {
        write(entity, o.common.field_432b1f5b, snapshot.field_144 + snapshot.field_130);
        write(entity, o.common.field_0d560fac, bindings.time);
    }
    auto* collision = engine::invoke<void*>(entity, 3);
    auto* maximum = engine::invoke<Vector*>(collision, 2);
    const auto old_height = (*maximum)[2];
    (*maximum)[2] = selected->header.collision_max_z;
    const auto old_duck = read<std::uint32_t>(entity, o.common.duck_amount);
    write(entity, o.common.duck_amount, snapshot.field_134);
    const auto old_6e = read<std::uint32_t>(entity, o.field_6e9852d6);
    const auto old_2a = read<Vector>(entity, o.field_2a671b42);
    write(entity, o.field_2a671b42, selected->header.origin);
    const auto old_4e = read<Vector>(entity, o.field_4e66bb34);
    write(entity, o.field_4e66bb34, selected->header.origin);
    const auto old_flags = read<std::uint32_t>(entity, o.field_83a6cf2b);
    write(entity, o.field_83a6cf2b, old_flags & 0xFFFFF7FFu);
    refresh_hitbox_attachments(entity, bindings.model, output, scratch, o.common.bone_accessor,
                               bindings.update_attachments);
    write(entity, o.field_83a6cf2b, old_flags);
    write(entity, o.field_4e66bb34, old_4e);
    write(entity, o.field_2a671b42, old_2a);
    write(entity, o.field_6e9852d6, old_6e);
    write(entity, o.common.duck_amount, old_duck);
    (*maximum)[2] = old_height;
    write(entity, o.field_e23a9be1, old_e23);
    write(entity, o.common.field_0d560fac, old_0d);
    write(entity, o.common.field_432b1f5b, old_432);
    return result;
}
bool prepare_target_bones(const ProfileState& state, const lagcomp::RecordRing& ring, void* entity,
                          std::span<Matrix3x4, 19> output, std::span<Matrix3x4, 256> scratch,
                          float& interval, const HistoricalBoneBindings& bindings) {
    if (state.reaction_time > 0.0f && ring.count) {
        const auto delay =
            _mm_cvttss_si32(_mm_set_ss(state.reaction_time / bindings.interval + 0.5f));
        const auto tick = historical_target_bones(
            ring, entity, subtract(bindings.current_tick, delay), output, scratch, bindings);
        if (tick != -1) {
            const auto age = float(subtract(bindings.current_tick, tick)) * bindings.interval;
            if (age > 0.0f && !(0.3f < age))
                interval = age;
            return true;
        }
    }
    const auto success = engine::invoke<bool>(static_cast<std::byte*>(entity) + 4, 13,
                                              scratch.data(), 256, 0xFFF00u, bindings.frame_time);
    if (!success)
        return false;
    animation::select_hitbox_bone_matrices(bindings.model, scratch, output);
    return true;
}
} // namespace skeet::legit
