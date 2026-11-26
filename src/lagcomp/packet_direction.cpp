#include "skeet/lagcomp/packet_direction.hpp"
#include "skeet/lagcomp/packet_capture.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::lagcomp {
namespace {
constexpr float epsilon = 0x1p-23f;
float component(const PacketTimingSample& sample, unsigned index) noexcept {
    return std::bit_cast<float>(sample.fields_04[index]);
}
Vector horizontal_difference(const PacketTimingSample& newer,
                             const PacketTimingSample& older) noexcept {
    return {component(newer, 0) - component(older, 0), component(newer, 1) - component(older, 1),
            0.0f};
}
Vector normalize(Vector value) noexcept {
    const float length =
        std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]);
    const float scale = 1.0f / (length + epsilon);
    for (auto& item : value)
        item = scale * item;
    return value;
}
float dot(const Vector& first, const Vector& second) noexcept {
    return (first[0] * second[0] + first[1] * second[1]) + first[2] * second[2];
}
}
bool update_history_packet_direction(HistoryPacketDirection& state, const LagTimeline& timeline,
                                     RecordRing& records, std::uint32_t flags,
                                     std::int32_t last_tick, float interval) noexcept {
    if ((flags & 1u) || timeline.packet_count < 2)
        return false;
    const auto first = timeline.packet_head % 6u;
    const auto second = (timeline.packet_head + 1u) % 6u;
    const auto third = (timeline.packet_head + 2u) % 6u;
    const auto& newest = timeline.packets[first];
    const auto& previous = timeline.packets[second];
    if (!(component(newest, 3) > 0.0005000000237487257f))
        return false;
    const bool older_stopped = timeline.packet_count >= 3 &&
                               0.0010000000474974513f > component(timeline.packets[third], 3);
    const bool previous_stopped = 0.0010000000474974513f > component(previous, 3);
    if (!previous_stopped && !older_stopped) {
        if (timeline.packet_count < 3)
            return false;
        const auto before = normalize(horizontal_difference(previous, timeline.packets[third]));
        const auto current = normalize(horizontal_difference(newest, previous));
        if (!(std::fabs(dot(before, current)) >= 0.9999904632568359f))
            return false;
        state.direction = current;
        state.time = float(last_tick) * interval;
        return true;
    }
    state.direction = normalize(horizontal_difference(newest, previous));
    state.time = float(last_tick) * interval;
    if (older_stopped && records.count) {
        auto& record = *records.records[records.first & 31u];
        if (!(record.header.flags & 0x10u)) {
            const auto& velocity = record.header.velocity;
            const auto direction = normalize({velocity[0], velocity[1], 0.0f});
            if (dot(direction, state.direction) >= 0.9961947202682495f) {
                const float speed =
                    std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
                record.header.velocity[0] = speed * state.direction[0];
                record.header.velocity[1] = speed * state.direction[1];
            }
        }
    }
    return true;
}
bool update_history_packet_direction(client::PlayerState& owner, std::uint32_t flags,
                                     std::int32_t last_tick, float interval) noexcept {
    HistoryPacketDirection state;
    auto* native = reinterpret_cast<std::byte*>(&owner.native);
    std::memcpy(state.direction.data(), native + 0x16C, sizeof(Vector));
    std::memcpy(&state.time, native + 0x19C, sizeof(float));
    if (!update_history_packet_direction(state, history_timeline(owner), owner.history, flags,
                                         last_tick, interval))
        return false;
    std::memcpy(native + 0x16C, state.direction.data(), sizeof(Vector));
    std::memcpy(native + 0x19C, &state.time, sizeof(float));
    return true;
}
} // namespace skeet::lagcomp
