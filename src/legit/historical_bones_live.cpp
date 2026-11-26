#include "skeet/legit/historical_bones_live.hpp"
#include "skeet/client/player_state.hpp"
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
std::int32_t subtract(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - std::uint32_t(second));
}
}
std::int32_t historical_target_bones_live(const lagcomp::RecordRing& ring, void* target,
                                          std::int32_t requested, std::span<Matrix3x4, 19> output,
                                          std::span<Matrix3x4, 256> scratch,
                                          const LiveHistoricalBoneBindings& bindings) {
    const lagcomp::LagRecord *previous = nullptr, *selected = nullptr;
    for (std::uint32_t ordinal = 0; ordinal != ring.count; ++ordinal) {
        const auto& record = ring.at(ordinal);
        if (previous) {
            const auto y = previous->header.origin[1] - record.header.origin[1];
            const auto x = previous->header.origin[0] - record.header.origin[0];
            const auto z = previous->header.origin[2] - record.header.origin[2];
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
        const auto old_time = float(selected->header.simulation_tick) * (*bindings.interval);
        const auto fraction =
            (float(requested) * (*bindings.interval) - old_time) /
            (float(previous->header.simulation_tick) * (*bindings.interval) - old_time);
        for (auto& matrix : output)
            for (unsigned axis = 0; axis != 3; ++axis)
                matrix.rows[axis][3] +=
                    (previous->header.origin[axis] - selected->header.origin[axis]) * fraction;
    }
    const auto& offsets = bindings.offsets;
    const auto& cached = bindings.cached;
    const auto old_432 = read<std::uint32_t>(target, offsets.common.field_432b1f5b);
    write(target, offsets.common.field_432b1f5b, read<std::uint32_t>(&cached, 0x12C));
    const auto old_0d = read<std::uint32_t>(target, offsets.common.field_0d560fac);
    write(target, offsets.common.field_0d560fac, read<std::uint32_t>(&cached, 0x148));
    const auto old_e23 = read<std::array<std::uint32_t, 2>>(target, offsets.field_e23a9be1);
    write(target, offsets.field_e23a9be1, read<std::array<std::uint32_t, 2>>(&cached, 0x1A8));
    if (selected->header.collision_max_z != read<float>(&cached, 0x130)) {
        write(target, offsets.common.field_432b1f5b,
              read<float>(&cached, 0x144) + read<float>(&cached, 0x130));
        write(target, offsets.common.field_0d560fac, *bindings.time);
    }
    auto* collision = engine::invoke<void*>(target, 3);
    auto* maximum = engine::invoke<Vector*>(collision, 2);
    const auto old_height = (*maximum)[2];
    (*maximum)[2] = selected->header.collision_max_z;
    const auto old_duck = read<std::uint32_t>(target, offsets.common.duck_amount);
    write(target, offsets.common.duck_amount, read<std::uint32_t>(&cached, 0x134));
    const auto old_6e = read<std::uint32_t>(target, offsets.field_6e9852d6);
    const auto old_2a = read<Vector>(target, offsets.field_2a671b42);
    write(target, offsets.field_2a671b42, selected->header.origin);
    const auto old_4e = read<Vector>(target, offsets.field_4e66bb34);
    write(target, offsets.field_4e66bb34, selected->header.origin);
    const auto old_flags = read<std::uint32_t>(target, offsets.field_83a6cf2b);
    write(target, offsets.field_83a6cf2b, old_flags & 0xFFFFF7FFu);
    bindings.bones.refresh_hitbox_attachments(target, output, scratch);
    write(target, offsets.field_83a6cf2b, old_flags);
    write(target, offsets.field_4e66bb34, old_4e);
    write(target, offsets.field_2a671b42, old_2a);
    write(target, offsets.field_6e9852d6, old_6e);
    write(target, offsets.common.duck_amount, old_duck);
    (*maximum)[2] = old_height;
    write(target, offsets.field_e23a9be1, old_e23);
    write(target, offsets.common.field_0d560fac, old_0d);
    write(target, offsets.common.field_432b1f5b, old_432);
    return result;
}
bool prepare_target_bones_live(const ProfileState& profile, const lagcomp::RecordRing& ring,
                               void* target, std::span<Matrix3x4, 19> output,
                               std::span<Matrix3x4, 256> scratch, float& interval,
                               const LiveHistoricalBoneBindings& bindings) {
    if (profile.reaction_time > 0.0f && ring.count) {
        const auto current = *bindings.current_tick;
        const auto delay =
            _mm_cvttss_si32(_mm_set_ss(profile.reaction_time / *bindings.interval + 0.5f));
        const auto selected = historical_target_bones_live(ring, target, subtract(current, delay),
                                                           output, scratch, bindings);
        if (selected != -1) {
            const auto age = float(subtract(current, selected)) * (*bindings.interval);
            if (age > 0.0f && !(0.3f < age))
                interval = age;
            return true;
        }
    }
    if (!engine::invoke<bool>(static_cast<std::byte*>(target) + 4, 13, scratch.data(), 256,
                              0xFFF00u, *bindings.frame_time))
        return false;
    bindings.bones.select_hitbox_bone_matrices(target, scratch, output);
    return true;
}
} // namespace skeet::legit
