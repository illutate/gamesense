#include "scanning_detail.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::rage::scan_detail {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::uint32_t convar(const void* object, std::ptrdiff_t offset) noexcept {
    for (;;) {
        const auto parent = read<const void*>(object, 0x1c);
        if (parent == object)
            break;
        object = parent;
    }
    return read<std::uint32_t>(object, offset) ^
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
//4341CB0C consumes the supplied axis verbatim. This caller passes the selected
//point vector, not a newly normalized segment direction.
float teammate_distance(const RageVector& center, const RageVector& origin, const RageVector& axis,
                        float bound) noexcept {
    const auto projection =
        ((center[1] - origin[1]) * axis[1] + (center[0] - origin[0]) * axis[0]) +
        (center[2] - origin[2]) * axis[2];
    if (projection < 0.0f || projection > bound)
        return std::numeric_limits<float>::max();
    const auto x = center[0] - (axis[0] * projection + origin[0]);
    const auto y = center[1] - (axis[1] * projection + origin[1]);
    const auto z = center[2] - (axis[2] * projection + origin[2]);
    return std::sqrt((x * x + y * y) + z * z);
}
}
Model model(void* player, const ScanModelOffsets& offsets) {
    Model result;
    const auto wrapper = read<const void*>(player, offsets.studio_header);
    if (!wrapper)
        return result;
    result.header = read<const void*>(wrapper, 0);
    if (!result.header)
        return result;
    const auto index = read<std::int32_t>(player, offsets.hitbox_set);
    const auto set = static_cast<const std::byte*>(result.header) +
                     read<std::int32_t>(result.header, 0xb0) + std::ptrdiff_t(index) * 12;
    const auto count = read<std::uint32_t>(set, 4);
    const auto capped = count < 19 ? count : 19U;
    const auto descriptors = set + read<std::int32_t>(set, 8);
    result.hitboxes.reserve(capped);
    result.bones.reserve(capped);
    for (std::uint32_t i = 0; i < capped; ++i) {
        const auto descriptor = descriptors + std::ptrdiff_t(i) * 0x44;
        result.bones.push_back(read<std::uint32_t>(descriptor, 0));
        result.hitboxes.push_back(
            {read<std::int32_t>(descriptor, 4), read<RageVector>(descriptor, 8),
             read<RageVector>(descriptor, 0x14), read<RageVector>(descriptor, 0x24),
             read<float>(descriptor, 0x30), descriptor});
    }
    return result;
}
bool teammate_blocks(const ScanContext& context, const RageVector& point) {
    const auto& variables = context.teammate_convars;
    const auto first = convar(variables.mode_43467c3c, 0x30) != 0 &&
                       std::bit_cast<float>(convar(variables.coefficient_43467084, 0x2c)) > 0.0f;
    if (!first && std::bit_cast<float>(convar(variables.coefficient_43468c90, 0x2c)) > 0.0f)
        return false;
    const RageVector delta{point[0] - context.origin[0], point[1] - context.origin[1],
                           point[2] - context.origin[2]};
    const auto distance =
        std::sqrt((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2]);
    const RageRay ray{context.origin, delta};
    const auto& target = context.targeting;
    for (std::int32_t index = 1; index <= 64; ++index) {
        const auto player = engine::invoke<void*>(target.entity_list, 3, index);
        if (!player || !engine::invoke<bool>(player, 158))
            continue;
        if (read<std::uint8_t>(player, target.offsets.rejection_bytes[0]))
            continue;
        if (engine::invoke<std::int32_t>(player, 122) <= 0)
            continue;
        if (engine::invoke<bool>(static_cast<std::byte*>(player) + 8, 9))
            continue;
        if (read<std::uint8_t>(player, target.offsets.rejection_bytes[1]))
            continue;
        if (read<std::int32_t>(player, target.offsets.team) != context.team)
            continue;
        auto center = read<RageVector>(player, context.model_offsets.network_origin);
        const auto minimum = engine::invoke<const RageVector*>(player, 76);
        const auto maximum = engine::invoke<const RageVector*>(player, 77);
        center[2] = ((*maximum)[2] + (*minimum)[2]) * 0.5f + center[2];
        if (!(60.0f > teammate_distance(center, context.origin, point, distance)))
            continue;
        const auto local = model(player, context.model_offsets);
        if (!local.header || local.hitboxes.empty())
            continue;
        const auto count = read<std::int32_t>(local.header, 0x9c);
        const auto matrices = read<const Matrix3x4*>(player, context.model_offsets.bone_cache);
        RageTrace trace;
        // Native43364580 traverses nineteen studio records regardless of the
        //reported positive hitbox count; valid player models supply all19.
        for (std::uint32_t box = 0; box != 19; ++box) {
            const auto bone = std::bit_cast<std::int32_t>(local.bones[box]);
            if (bone < 0 || bone >= count)
                continue;
            const auto prepared = prepare_rage_scan_hitbox(
                local.hitboxes[box], matrices[std::size_t(bone)], context.trigonometry);
            if (context.box_clipper.clip(context.box_clipper.context, ray, prepared, trace) >= 0)
                return true;
        }
    }
    return false;
}
}
