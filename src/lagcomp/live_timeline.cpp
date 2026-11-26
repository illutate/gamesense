#include "skeet/lagcomp/live_timeline.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/client/command_orchestrator.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>

namespace skeet::lagcomp {
namespace {
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t subtract(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - std::uint32_t(second));
}
std::int32_t cap(std::int32_t value, std::int32_t limit) noexcept {
    if (limit < 1)
        return limit;
    if (value < 1)
        return 1;
    return value > limit ? limit : value;
}
}

std::int32_t estimate_missing_ticks(const LagTimeline& timeline, const MissingTickTiming& timing,
                                    const client::CommandBudgetBindings& budget) {
    const auto now = add(timing.tick_d4, timing.tick_18c);
    const auto simulation = time_to_tick(timeline.simulation_time, timing.interval);
    const auto distance = subtract(now, simulation);
    const auto sign = distance < 0 ? UINT32_MAX : 0u;
    const auto absolute = std::bit_cast<std::int32_t>((std::uint32_t(distance) ^ sign) - sign);
    if (absolute >= 128)
        return 0;
    std::array<std::int32_t, 10> pattern;
    std::size_t count = 0;
    bool simple = timeline.record_count < 2 || timeline.packet_count < 2;
    if (!simple && simulation < subtract(timeline.last_update_tick, 1) &&
        simulation < timeline.at(0).header.simulation_tick)
        simple = true;
    if (!simple && packet_timing_discontinuity(timeline))
        simple = true;
    if (!simple && (timeline.at(0).header.flags & 0x200u))
        simple = true;
    if (!simple) {
        const auto head = timeline.packet_head % 6u, next = (timeline.packet_head + 1u) % 6u;
        if (subtract(timeline.packets[head].field_18, timeline.packets[next].field_18) < 4 &&
            subtract(timeline.at(0).header.receive_tick, timeline.at(1).header.receive_tick) > 7)
            simple = true;
    }
    if (simple)
        pattern[count++] = 2;
    else {
        const LagRecord* previous = nullptr;
        for (std::uint32_t age = 1; age < timeline.record_count; ++age) {
            const auto& record = timeline.at(age);
            if (previous && !(previous->header.flags & 8u)) {
                const auto limit = client::query_command_budget(budget); //433F9602
                pattern[count++] =
                    cap(subtract(previous->header.simulation_tick, record.header.simulation_tick),
                        limit);
                if (count == 10)
                    break;
            }
            if (record.header.flags & 0x200u)
                break;
            previous = &record;
        }
        const auto latest_limit = client::query_command_budget(budget); //433F9669
        const auto latest_gap = cap(
            subtract(timeline.at(0).header.simulation_tick, timeline.at(1).header.simulation_tick),
            latest_limit);
        const auto arrival_limit = client::query_command_budget(budget); //433F969A, before engine14
        const auto received = sample_receive_tick(timing.engine, timing.receive_interval);
        const auto arrival_gap =
            cap(add(subtract(received, timeline.at(0).header.receive_tick), 1), arrival_limit);
        if (count < 10)
            pattern[count++] = latest_gap >= arrival_gap ? latest_gap : arrival_gap;
    }
    std::uint32_t index = 0;
    auto step = pattern[0];
    auto position = add(timeline.last_update_tick, step);
    std::int32_t total = 0;
    while (position <= now) {
        if (position == now) {
            const auto player_index =
                engine::invoke<std::int32_t>(static_cast<std::byte*>(timing.player) + 8, 10);
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
