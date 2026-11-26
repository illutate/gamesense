#include "skeet/lagcomp/history.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t absolute(std::int32_t value) noexcept {
    const auto sign = value < 0 ? UINT32_MAX : 0u;
    return std::bit_cast<std::int32_t>((std::uint32_t(value) ^ sign) - sign);
}
float field(const LagRecord& record, std::size_t offset) noexcept {
    float value;
    std::memcpy(&value, reinterpret_cast<const std::byte*>(&record) + offset, 4);
    return value;
}
float origin_distance(const LagRecord& first, const LagRecord& second) noexcept {
    const auto& a = first.header.origin;
    const auto& b = second.header.origin;
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return (y * y + x * x) + z * z;
}
std::int32_t capped_gap(std::int32_t value, std::int32_t limit) noexcept {
    if (limit < 1)
        return limit;
    if (value < 1)
        return 1;
    return value > limit ? limit : value;
}
}
LagRecord& RecordPool::acquire() {
    LagRecord* record;
    if (free.empty()) {
        allocated.push_back(std::make_unique<LagRecord>());
        record = allocated.back().get();
    } else {
        record = free.back();
        free.pop_back();
        record->header.field_00 = 0;
    }
    return *record;
}
void RecordPool::release(LagRecord& record) {
    record.header.field_00 = std::uint32_t(reinterpret_cast<std::uintptr_t>(free_head()));
    free.push_back(&record);
}
void RecordPool::finalize_free_records() {
    while (!free.empty()) {
        auto* const record = free.back();
        const auto owner =
            std::find_if(allocated.begin(), allocated.end(),
                         [record](const auto& candidate) { return candidate.get() == record; });
        if (owner == allocated.end())
            throw std::logic_error(
                "History terminal cleanup requires the canonical owning record pool");
        free.pop_back();
        record->header.field_00 = 0;
        owner->reset();
    }
    allocated.erase(std::remove_if(allocated.begin(), allocated.end(),
                                   [](const auto& record) { return !record; }),
                    allocated.end());
    std::vector<LagRecord*>{}.swap(free);
    if (allocated.empty())
        std::vector<std::unique_ptr<LagRecord>>{}.swap(allocated);
}
LagRecord& push_history_record(RecordRing& ring, RecordPool& pool) {
    LagRecord* record;
    if (ring.count == 32)
        record = ring.records[(ring.first - 1u) & 31u];
    else {
        record = &pool.acquire();
        ++ring.count;
    }
    ring.first = (ring.first - 1u) & 31u;
    ring.records[ring.first] = record;
    std::memset(record, 0, sizeof(*record));
    return *record;
}
void pop_history_record(RecordRing& ring, RecordPool& pool) {
    auto* record = ring.records[(ring.first - 1u + ring.count) & 31u];
    if (record)
        pool.release(*record);
    if (ring.count)
        --ring.count;
}
void clear_history_records(RecordRing& ring, RecordPool& pool) {
    while (ring.count)
        pop_history_record(ring, pool);
}
void prune_history_records(RecordRing& ring, RecordPool& pool, std::int32_t clock, float interval) {
    const auto maximum_age = time_to_tick(1.0f, interval);
    while (ring.count &&
           absolute(sub(ring.at(ring.count - 1u).header.receive_tick, clock)) >= maximum_age)
        pop_history_record(ring, pool);
}
void prune_packet_samples(LagTimeline& timeline, std::int32_t clock, float interval) noexcept {
    const auto maximum_age = time_to_tick(1.0f, interval);
    while (timeline.packet_count) {
        const auto index = (timeline.packet_head - 1u + timeline.packet_count) % 6u;
        if (absolute(sub(timeline.packets[index].field_18, clock)) < maximum_age)
            break;
        --timeline.packet_count;
    }
}
ExtrapolationPlan plan_history_extrapolation(const RecordRing& ring, std::uint32_t ordinal,
                                             std::uint32_t options, float interval,
                                             std::int32_t prediction_limit) noexcept {
    ExtrapolationPlan result;
    if (!(options & 1u) || ordinal || ring.count < 2)
        return result;
    const auto& newest = ring.at(0);
    const auto& previous = ring.at(1);
    if (newest.header.flags & 0x200u)
        return result;
    if (newest.header.player_flags & 1u) {
        if (previous.header.player_flags & 1u)
            return result;
        const auto weight = field(newest, 0xFC);
        const auto rate = field(newest, 0x104);
        const auto rate_bits = std::bit_cast<std::uint32_t>(rate) & 0x7FFFFFFFu;
        if (weight > 0.0f && rate_bits && time_to_tick(field(newest, 0x108) / rate, interval) > 1)
            return result;
    }
    const LagRecord* first = &newest;
    const LagRecord* second = &previous;
    if (!(origin_distance(newest, previous) > 4096.0f)) {
        if (ring.count == 2 || (previous.header.flags & 0x200u))
            return result;
        result.inspected_third = true;
        const auto& third = ring.at(2);
        if (!(origin_distance(previous, third) > 4096.0f))
            return result;
        const auto& a = newest.header.velocity;
        const auto& b = previous.header.velocity;
        const auto speed_a = std::sqrt(a[0] * a[0] + a[1] * a[1]);
        const auto speed_b = std::sqrt(b[0] * b[0] + b[1] * b[1]);
        if (speed_b - speed_a > 50.0f)
            return result;
        first = &previous;
        second = &third;
    }
    result.ticks = capped_gap(sub(first->header.simulation_tick, second->header.simulation_tick),
                              prediction_limit);
    return result;
}
HistoryTickTiming resolve_history_tick_timing(const HistoryTickTiming& source) noexcept {
    auto result = source;
    if (source.live_globals) {
        result.current_time = *source.live_globals->current_time;
        result.interpolation = source.live_globals->interpolation;
        result.interval = *source.live_globals->interval;
        result.maximum_unlag_ms = source.live_globals->maximum_unlag_ms;
    }
    if (source.live_tick_d4)
        result.tick_d4 = std::bit_cast<std::int32_t>(*source.live_tick_d4);
    if (source.live_tick_18c)
        result.tick_18c = std::bit_cast<std::int32_t>(*source.live_tick_18c);
    result.live_globals = nullptr;
    result.live_tick_d4 = nullptr;
    result.live_tick_18c = nullptr;
    return result;
}
HistoryTickWindow history_tick_window(const HistoryTickTiming& source) noexcept {
    const auto timing = resolve_history_tick_timing(source);
    const float maximum = float(timing.maximum_unlag_ms) * 0.001f;
    const float delay = _mm_cvtss_f32(
        _mm_min_ss(_mm_set_ss(timing.latency_198 + timing.interpolation), _mm_set_ss(maximum)));
    const float centered = timing.current_time - delay;
    const auto first = add(time_to_tick(centered - 0.2f, timing.interval), 1);
    const auto last = sub(time_to_tick(centered + 0.2f, timing.interval), 1);
    const auto seconds = _mm_cvtt_ss2si(
        _mm_set_ss(float(add(timing.tick_d4, timing.tick_18c)) * timing.interval - maximum));
    return {first, last, time_to_tick(float(seconds), timing.interval)};
}
std::int32_t mean_history_timing_ticks(const TickTimingSamples& samples) noexcept {
    if (samples.count <= 0)
        return 1;
    auto sum = _mm_setzero_ps();
    for (std::int32_t i = 0; i < samples.count; ++i)
        sum = _mm_add_ss(sum, _mm_set_ss(samples.values[(samples.first + std::uint32_t(i)) & 15u]));
    return _mm_cvt_ss2si(_mm_div_ss(sum, _mm_cvtsi32_ss(_mm_setzero_ps(), samples.count)));
}
void publish_record_flags(LagRecord& record, const LagRecord* previous, bool compare_layer,
                          std::uint32_t update_flags, bool flag_43468d7c) noexcept {
    auto& flags = record.header.flags;
    if (previous && compare_layer && !(flags & 0x200u) && !(previous->header.flags & 0x200u) &&
        field(*previous, 0x118) == 0.0f && field(record, 0x118) > 0.0f)
        flags |= 0x400u;
    if (update_flags & 1u)
        flags |= 0x200u;
    if (update_flags & 4u) {
        flags |= 8u;
        if (flag_43468d7c)
            flags = (flags & 0xFFFFEFFFu) | 0x800u;
    }
    if (update_flags & 0x10u)
        flags |= 2u;
    if (update_flags & 0x20u)
        flags |= 0x20u;
    if (update_flags & 8u)
        flags |= 0x10u;
}
HistoryCaptureSlot prepare_history_capture(HistoryRecordingState& state, RecordPool& pool,
                                           bool enabled, bool suppressed,
                                           std::int32_t simulation_tick, bool refresh_fallback) {
    if (!enabled || suppressed) {
        clear_history_records(state.ring, pool);
        state.resolver_index = 0;
        return {nullptr, HistoryCaptureKind::none};
    }
    if (!state.ring.count || simulation_tick > state.ring.at(0).header.simulation_tick)
        return {&push_history_record(state.ring, pool), HistoryCaptureKind::inserted};
    if (refresh_fallback) {
        std::memset(&state.fallback, 0, sizeof(state.fallback));
        return {&state.fallback, HistoryCaptureKind::fallback};
    }
    return {nullptr, HistoryCaptureKind::none};
}
bool publish_history_fallback(HistoryRecordingState& state, bool captured_now, void* engine,
                              float interval, float receive_interval,
                              std::int32_t maximum_unlag_ms) {
    if (!state.ring.count ||
        state.fallback.header.receive_tick <= state.ring.at(0).header.receive_tick)
        return false;
    const auto receive_tick =
        receive_time_to_tick(engine::invoke<float>(engine, 14), receive_interval);
    if (!captured_now) {
        const float seconds = float(receive_tick) * interval - float(maximum_unlag_ms) * 0.001f;
        const auto truncated = _mm_cvtt_ss2si(_mm_set_ss(seconds));
        if (float(truncated) > float(state.ring.at(0).header.simulation_tick) * interval) {
            std::memcpy(&state.ring.at(0), &state.fallback, sizeof(LagRecord));
            state.fallback.header.receive_tick = -1;
            return true;
        }
    }
    state.flags |= 0x100u;
    return false;
}
} // namespace skeet::lagcomp
