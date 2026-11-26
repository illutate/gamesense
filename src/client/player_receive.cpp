#include "skeet/client/player_receive.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/movement/steering_math.hpp"
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::uint32_t index(void* entity) {
    return engine::invoke<std::uint32_t>(static_cast<std::byte*>(entity) + 8, 10);
}
NativePlayerStateLayout& state(PlayerStateCache& players, std::uint32_t id) {
    auto* result = players.get(id);
    if (!result)
        throw std::bad_alloc();
    return result->native;
}
}
void PlayerReceiveRuntime::original(std::uint32_t cell, const engine::RecvProxyData* packet,
                                    void* entity, void* output) const {
    data_.native_cell<engine::RecvVarProxy>(cell)(packet, entity, output);
}
void PlayerReceiveRuntime::simulation_time(const engine::RecvProxyData* packet, void* entity,
                                           void* output) {
    original(0x434769E4u, packet, entity, output);
    const auto id = index(entity);
    if (id - 1u > 63u || !engine::invoke<bool>(entity, 158))
        return;
    auto& owner = state(players_, id);
    const auto& fields = offsets_.globals;
    const auto offset =
        *fields.pointers.at(0x43469198u) ^ fields.keys.at(0x4346A538u) ^ 0xD5FD2E2Cu;
    write(&owner, 0x10C, read<std::uint32_t>(entity, offset));
}
void PlayerReceiveRuntime::timestamped(const engine::RecvProxyData* packet, void* entity,
                                       void* output, std::uint32_t saved, std::size_t value_offset,
                                       std::size_t stamp_offset) {
    original(saved, packet, entity, output);
    const auto id = index(entity);
    if (id - 1u > 63u || !engine::invoke<bool>(entity, 158))
        return;
    auto& owner = state(players_, id);
    write(&owner, value_offset, read<std::uint32_t>(packet, 8));
    const auto tick = lagcomp::sample_receive_tick(data_.native_cell<void*>(0x4346A514u),
                                                   data_.native_cell<std::byte*>(0x43467B04u));
    write(&owner, stamp_offset, tick);
}
void PlayerReceiveRuntime::tick_base(const engine::RecvProxyData* packet, void* entity,
                                     void* output) {
    timestamped(packet, entity, output, 0x43476AC4u, 0x1E0, 0x1E4);
}
void PlayerReceiveRuntime::velocity(const engine::RecvProxyData* packet, void* entity, void* output,
                                    unsigned component) {
    constexpr std::uint32_t saved[] = {0x43476954u, 0x43476A04u, 0x43476A64u};
    if (component >= 3)
        throw std::out_of_range("Receive velocity component");
    timestamped(packet, entity, output, saved[component], 0x1C8 + component * 4, 0x1DC);
}
void PlayerReceiveRuntime::eye_angle(const engine::RecvProxyData* packet, void* entity,
                                     void* output, unsigned component) {
    constexpr std::uint32_t saved[] = {0x43476994u, 0x43476A54u};
    if (component >= 2)
        throw std::out_of_range("Receive eye angle component");
    const auto id = index(entity);
    if (id - 1u <= 63u) {
        auto& owner = state(players_, id);
        write(&owner, 0x1A8 + component * 4, read<std::uint32_t>(packet, 8));
        // The engine packet is mutable; the saved proxy consumes this value.
        write(const_cast<engine::RecvProxyData*>(packet), 8,
              normalize_movement_angle(read<float>(packet, 8)));
    }
    original(saved[component], packet, entity, output);
}
void PlayerReceiveRuntime::client_animation(const engine::RecvProxyData* packet, void* entity,
                                            void* output) {
    const auto id = index(entity);
    if (id - 1u <= 63u) {
        const auto incoming = std::uint8_t(read<std::uint32_t>(packet, 8) != 0);
        auto& owner = state(players_, id);
        write(&owner, 0x17C, incoming);
    }
    original(0x434769D4u, packet, entity, output);
}
} //namespace skeet::client
