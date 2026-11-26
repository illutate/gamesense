#include "skeet/lagcomp/timeline.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>

namespace skeet::lagcomp {
namespace {
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t cap(std::int32_t value, std::int32_t limit) noexcept {
    if (limit < 1)
        return limit;
    if (value < 1)
        return 1;
    return value > limit ? limit : value;
}
}
bool packet_timing_discontinuity(const LagTimeline& timeline) noexcept {
    for (std::uint32_t i = 0; i != timeline.packet_count; ++i) {
        const auto& sample = timeline.packets[(timeline.packet_head + i) % 6u];
        if (sample.field_00 < sub(sample.field_18, 1))
            return true;
    }
    return false;
}
std::int32_t prediction_tick_limit(bool available, bool first, bool second, std::int32_t mode,
                                   std::int32_t configured) noexcept {
    if (available) {
        if (!first)
            return configured;
        if (second && mode == 1)
            return 3;
    }
    return 7;
}
std::int32_t estimate_missing_ticks(const LagTimeline& timeline, const MissingTickTiming& timing) {
    const auto now = add(timing.tick_d4, timing.tick_18c);
    const auto simulation = time_to_tick(timeline.simulation_time, timing.interval);
    const auto distance = sub(now, simulation);
    const auto sign = distance < 0 ? UINT32_MAX : 0u;
    const auto absolute = std::bit_cast<std::int32_t>((std::uint32_t(distance) ^ sign) - sign);
    if (absolute >= 128)
        return 0;
    std::array<std::int32_t, 10> pattern{};
    std::size_t count = 0;
    bool simple = timeline.record_count < 2 || timeline.packet_count < 2;
    if (!simple && simulation < sub(timeline.last_update_tick, 1) &&
        simulation < timeline.at(0).header.simulation_tick)
        simple = true;
    if (!simple && packet_timing_discontinuity(timeline))
        simple = true;
    if (!simple && (timeline.at(0).header.flags & 0x200u))
        simple = true;
    if (!simple) {
        const auto head = timeline.packet_head % 6u;
        const auto next = (timeline.packet_head + 1u) % 6u;
        if (sub(timeline.packets[head].field_18, timeline.packets[next].field_18) < 4 &&
            sub(timeline.at(0).header.receive_tick, timeline.at(1).header.receive_tick) > 7)
            simple = true;
    }
    if (simple)
        pattern[count++] = 2;
    else {
        const LagRecord* previous = nullptr;
        for (std::uint32_t age = 1; age < timeline.record_count; ++age) {
            const auto& record = timeline.at(age);
            if (previous && !(previous->header.flags & 8u)) {
                pattern[count++] =
                    cap(sub(previous->header.simulation_tick, record.header.simulation_tick),
                        timing.prediction_limit);
                if (count == 10)
                    break;
            }
            if (record.header.flags & 0x200u)
                break;
            previous = &record;
        }
        const auto latest_gap =
            cap(sub(timeline.at(0).header.simulation_tick, timeline.at(1).header.simulation_tick),
                timing.prediction_limit);
        const float received = engine::invoke<float>(timing.engine, 14);
        const auto receive_tick = receive_time_to_tick(received, timing.receive_interval);
        const auto arrival_gap = cap(add(sub(receive_tick, timeline.at(0).header.receive_tick), 1),
                                     timing.prediction_limit);
        if (count < 10)
            pattern[count++] = latest_gap >= arrival_gap ? latest_gap : arrival_gap;
    }
    std::uint32_t index = 0;
    auto step = pattern[0];
    auto position = add(timeline.last_update_tick, step);
    std::int32_t total = 0;
    while (position <= now) {
        if (position == now) {
            auto* networkable = static_cast<std::byte*>(timing.player) + 8;
            const auto player_index = engine::invoke<std::int32_t>(networkable, 10);
            if (engine::invoke<std::int32_t>(timing.engine, 12) < player_index)
                break;
        }
        total = add(total, step);
        ++index;
        step = pattern[index % count];
        position = add(position, step);
    }
    return total;
}
} // namespace skeet::lagcomp
