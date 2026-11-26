#include "skeet/client/player_data_update.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/offset_storage.hpp"
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void store(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
}
PlayerDataUpdateOffsets
native_player_data_update_offsets(const engine::NativeOffsetGlobals& globals) {
    const auto decode = [&](std::uint32_t pointer, std::uint32_t mask, std::uint32_t key) {
        return std::ptrdiff_t(*globals.pointers.at(pointer) ^ globals.keys.at(mask) ^ key);
    };
    return {decode(0x434679F8, 0x434675F8, 0x3309C0CF), decode(0x43468354, 0x4346852C, 0x66146802),
            decode(0x43467230, 0x43467100, 0xD3D4230D), decode(0x43468FEC, 0x4346A3C4, 0xA2A8247F),
            decode(0x4346A8E4, 0x434688CC, 0x36375C23), decode(0x43468BF8, 0x43467B8C, 0xD070684C)};
}
void capture_serialized_player_state(NativePlayerStateLayout& cache, void* player,
                                     const PlayerDataUpdateOffsets& offsets) {
    auto* bytes = reinterpret_cast<std::byte*>(&cache);
    std::memcpy(bytes + 0x2FC, bytes + 0x468, 0x16C);
    store(&cache, 0x104, field<std::uint32_t>(player, offsets.duck_3309c0cf));
    store(&cache, 0x150, field<std::uint32_t>(player, offsets.scaled_recoil_66146802));
    store(&cache, 0x188, field<std::uint32_t>(&cache, 0x1A8));
    store(&cache, 0x114, field<std::uint32_t>(&cache, 0x1AC));
    store(&cache, 0x154, field<std::uint32_t>(player, offsets.replicated_time_d3d4230d));
    store(&cache, 0x184, field<std::uint32_t>(player, offsets.body_yaw_a2a8247f));
    const auto* virtual77 = engine::invoke<const std::byte*>(player, 77);
    store(&cache, 0x11C, field<std::uint32_t>(virtual77, 8));
    // The velocity read follows virtual77; the engine may change the player.
    for (std::size_t index = 0; index < 3; ++index)
        store(&cache, 0x120 + index * 4,
              field<std::uint32_t>(player, offsets.velocity_36375c23 + index * 4));
}
} //namespace skeet::client
