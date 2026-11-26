#include "skeet/lagcomp/packet_capture.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/resolver.hpp"
#include <cmath>
#include <cstring>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (std::fabs(value) > 180.0f) {
        const float turns = round_half_away_from_zero(std::fabs(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + turns : value - turns;
    }
    return value;
}
}
LagTimeline history_timeline(const client::PlayerState& owner) noexcept {
    LagTimeline result;
    result.simulation_time = read<float>(&owner.native, 0x110);
    result.last_update_tick = read<std::int32_t>(&owner.native, 0x168);
    result.packet_head = read<std::uint32_t>(&owner.native, 0x234);
    result.packet_count = owner.native.field_238;
    std::memcpy(result.packets.data(), reinterpret_cast<const std::byte*>(&owner.native) + 0x23C,
                sizeof(result.packets));
    result.record_head = owner.history.first;
    result.record_count = owner.history.count;
    for (unsigned i = 0; i != 32; ++i)
        result.records[i] = owner.history.records[i];
    return result;
}
void publish_history_eye_angles(client::PlayerState& owner, void* player, bool resolver_branch,
                                HistoryPitchOverride override, const EntityStateRuntime& runtime) {
    if (resolver_branch) {
        owner.native.resolver_index = 0;
        if (!override.enabled)
            write(player, runtime.offsets.eye_angles, normalize(read<float>(&owner.native, 0x1A8)));
        write(player, runtime.offsets.eye_angles + 4, normalize(owner.native.reference_yaw));
    }
    if (override.enabled)
        write(player, runtime.offsets.eye_angles, float(override.pitch));
}
void capture_history_packet(client::PlayerState& owner, bool changed, void* player,
                            const EntityStateRuntime& runtime, std::uint32_t flags_offset) {
    if (!changed)
        return;
    write(&owner.native, 0x18C, read<std::uint32_t>(&owner.native, 0x188));
    write(&owner.native, 0x190, read<std::uint32_t>(&owner.native, 0x114));
    owner.native.flags &= ~0x100u;
    if (owner.native.field_238 != 6u)
        ++owner.native.field_238;
    const auto first = (read<std::uint32_t>(&owner.native, 0x234) + 5u) % 6u;
    write(&owner.native, 0x234, first);
    const auto offset = std::size_t(0x23C) + first * 0x20u;
    write(&owner.native, offset, time_to_tick(read<float>(&owner.native, 0x110), runtime.interval));
    write(&owner.native, offset + 4, read<Vector>(player, runtime.offsets.origin));
    write(&owner.native, offset + 0x10, read<std::uint32_t>(&owner.native, 0x524));
    write(&owner.native, offset + 0x14, read<std::uint32_t>(player, flags_offset));
    const auto receive = sample_receive_tick(runtime.engine, runtime.receive_interval);
    write(&owner.native, offset + 0x18, receive);
    write(&owner.native, offset + 0x1C, owner.native.reference_yaw);
}
} // namespace skeet::lagcomp
