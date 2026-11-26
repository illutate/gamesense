#include "skeet/legit/studio.hpp"
#include <algorithm>
#include <cstring>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
StudioHitboxSet load_target_hitbox_set(const void* entity, const StudioOffsets& offsets) {
    const auto* wrapper = read<const void*>(entity, offsets.wrapper);
    if (!wrapper)
        return {nullptr, 0, false};
    const auto* header = read<const std::byte*>(wrapper, 0);
    if (!header)
        return {nullptr, 0, false};
    const auto* set = header + read<std::int32_t>(header, 0xB0) +
                      std::ptrdiff_t(read<std::int32_t>(entity, offsets.hitbox_set)) * 12;
    return {set + read<std::int32_t>(set, 8), read<std::int32_t>(set, 4), true};
}
TargetStudio load_target_studio(const void* entity, const StudioOffsets& offsets) {
    TargetStudio result;
    const auto set = load_target_hitbox_set(entity, offsets);
    result.ready = set.studio;
    result.count = std::max(0, std::min(19, set.count));
    for (std::int32_t i = 0; i < result.count; ++i) {
        const auto* box = set.descriptors + i * 0x44;
        result.bones[std::size_t(i)] = read<std::uint32_t>(box, 0);
        result.hitboxes[std::size_t(i)] = {read<std::int32_t>(box, 0), read<std::int32_t>(box, 4),
                                           read<Vector>(box, 8), read<Vector>(box, 0x14),
                                           read<float>(box, 0x30)};
    }
    return result;
}
} // namespace skeet::legit
