#include "skeet/client/command_prediction_cache.hpp"
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
}
void restore_command_prediction_cache(std::uint32_t flags, void* player,
                                      const lagcomp::EntityNetvars& offsets,
                                      const CommandBoneBackup& backup) {
    if (!(flags & 0x20u))
        return;
    write(player, offsets.bone_accessor + 8, std::uint32_t(0));
    write(player, offsets.recent_bone_counter, std::uint32_t(0));
    write(player, offsets.last_bone_setup_time, std::uint32_t(0xFF7FFFFF));
    write(player, offsets.field_0471e13c, std::uint32_t(0));
    write(player, offsets.effects_bb78dc43,
          read<std::uint32_t>(player, offsets.effects_bb78dc43) & 0xFFFFFFFDu);
    if (!backup.count)
        return;
    auto* cache = static_cast<std::byte*>(player) + offsets.bone_cache;
    const auto live = read<std::int32_t>(cache, 0xC);
    if (live < backup.count)
        return;
    std::memcpy(read<Matrix3x4*>(cache, 0), backup.storage, std::size_t(live) * sizeof(Matrix3x4));
}
} // namespace skeet::client
