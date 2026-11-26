#pragma once
#include "skeet/client/restricted_player_command.hpp"
#include <cstring>

namespace skeet::client::restricted_detail {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void update_animation_step(RestrictedPlayerCommandOwner&, void* player, void* animation_state,
                           Command&, bool jump_reset, std::int32_t seed, std::int32_t layer_count,
                           const std::array<float, 3>* replay_angles,
                           const RestrictedPlayerCommandBindings&);
}
