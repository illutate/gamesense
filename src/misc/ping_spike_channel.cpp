#include "skeet/misc/ping_spike_channel.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t increment(std::int32_t value) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) + 1u);
}
std::int32_t subtract_128(std::int32_t value) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(value) - 128u);
}
PingSpikeChannelRuntime* channel_runtime = nullptr;
}

void record_ping_spike_sequences(PingSpikeNetworkHistory& history, std::uint32_t direction,
                                 std::int32_t sequence, std::int32_t acknowledged,
                                 PingSpikeNetworkClock clock) {
    auto& flow = history[direction];
    const auto now = clock();
    const auto previous = flow.sequence;
    if (sequence > previous) {
        auto next = increment(previous);
        PingSpikeLatencySample* last = nullptr;
        for (std::uint32_t count = 0; next <= sequence && count < 128;
             ++count, next = increment(next)) {
            auto& sample = flow.samples[std::uint32_t(next) & 127u];
            sample.sent_at = now;
            sample.present = 0;
            sample.round_trip = -1.0;
            last = &sample;
        }
        last->present = 1;
    }
    flow.sequence = sequence;
    auto& opposite = history[direction == 0 ? 1 : 0];
    if (acknowledged <= subtract_128(opposite.sequence))
        return;
    auto& sample = opposite.samples[std::uint32_t(acknowledged) & 127u];
    if (!sample.present || sample.round_trip != -1.0)
        return;
    const auto elapsed = now - sample.sent_at;
    sample.round_trip = elapsed < 0.0 ? 0.0 : elapsed;
}

void average_ping_spike_sequences(PingSpikeNetworkHistory& history,
                                  std::uint32_t direction) noexcept {
    auto& flow = history[direction];
    double total = 0.0;
    std::int32_t count = 0;
    for (const auto& sample : flow.samples) {
        if (sample.present && sample.round_trip >= 0.0) {
            total += sample.round_trip;
            ++count;
        }
    }
    if (count <= 0)
        return;
    const auto average = total / double(count);
    const auto previous = flow.smoothed * 0.75;
    flow.raw_average = average;
    flow.smoothed = previous + average * 0.25;
}

float query_channel_interpolation(void* engine_client, const PingSpikeNetworkHistory& history) {
    auto* channel = engine::invoke<void*>(engine_client, 78);
    if (!channel)
        return 0.0f;
    std::array<float, 2> original;
    for (std::int32_t direction = 0; direction < 2; ++direction)
        original[direction] = engine::invoke<float>(channel, 9, direction);

    // Complete43406FB8(channel,13,13,1), followed by the native range check.
    auto** table = field<void**>(channel, 0);
    const auto operand = field<std::uint32_t>(table[13], 13);
    const auto displacement = operand - 4u;
    if ((displacement & 0xFFFu) != displacement)
        return original[1] + original[0];

    // Native stores ST0 directly to a qword here, without the single-precision
    // rounding used for the two virtual9 results above.
    const auto channel_time = engine::invoke<double>(channel, 2);
    const auto measured = float(history[0].raw_average + history[1].raw_average);
    const auto compared_time = double(measured) + channel_time;
    auto adjusted = original;
    auto* flow = static_cast<const std::byte*>(channel) + displacement;
    for (std::uint32_t direction = 0; direction < 2; ++direction, flow += 0x1E2C) {
        const auto sample_time = double(field<float>(flow, 0));
        if (!(1.0 > std::fabs(sample_time - compared_time)) || !(compared_time >= sample_time))
            continue;
        float total = 0.0f;
        std::int32_t count = 0;
        for (std::uint32_t sample = 0; sample < 128; ++sample) {
            if (!field<std::uint8_t>(flow, 0x32 + sample * 0x10))
                continue;
            const auto latency = field<float>(flow, 0x34 + sample * 0x10);
            if (!(latency > -1.0f))
                continue;
            total += latency;
            ++count;
        }
        if (count > 0)
            adjusted[direction] = total / float(count);
    }
    const auto original_sum = original[1] + original[0];
    const auto adjusted_sum = adjusted[1] + adjusted[0];
    return 1.0f > std::fabs(adjusted_sum - original_sum) ? adjusted_sum : original_sum;
}

std::int32_t
acknowledge_datagram_sequence(std::span<const client::StartupAcknowledgement, 150> history,
                              std::int32_t sequence) noexcept {
    const auto* record = reinterpret_cast<const std::byte*>(history.data()) +
                         std::ptrdiff_t(sequence % 150) * sizeof(client::StartupAcknowledgement);
    return field<std::uint32_t>(record, 0) == std::bit_cast<std::uint32_t>(sequence)
               ? field<std::int32_t>(record, 4)
               : sequence;
}

PingSpikeChannelRuntime::PingSpikeChannelRuntime(PingSpikeRuntime& runtime,
                                                 PingSpikeChannelBindings shared)
    : feature(runtime), bindings(shared) {
    feature.latency_4347a508 = &bindings.history[0].smoothed;
    feature.latency_4347b120 = &bindings.history[1].smoothed;
}

std::int32_t PingSpikeChannelRuntime::send_datagram(void* channel, void* datagram) {
    const auto original_sequence = field<std::int32_t>(channel, 0x1C);
    const auto skips = bindings.pending_rewind_skips;
    if (skips & 0xFFu) {
        for (std::uint32_t bit = 0; bit < 8; ++bit) {
            if (skips & (1u << bit)) {
                bindings.pending_rewind_skips = skips & ~(1u << bit);
                break;
            }
        }
    } else
        feature.rewind_sequence(channel);

    auto** table = field<void**>(channel, 0);
    auto** original = field<void**>(table[-2], 8);
    using SendDatagram = engine::MemberFunction<std::int32_t, void*>;
    const auto result = reinterpret_cast<SendDatagram>(original[46])(channel, datagram);
    record_ping_spike_sequences(bindings.history, 0, result, original_sequence, bindings.clock);
    average_ping_spike_sequences(bindings.history, 0);
    bindings.interpolation = query_channel_interpolation(bindings.engine_client, bindings.history);
    const auto acknowledged = acknowledge_datagram_sequence(bindings.acknowledgements, result);
    std::memcpy(static_cast<std::byte*>(channel) + 0x1C, &original_sequence,
                sizeof original_sequence);
    return acknowledged;
}
void bind_ping_spike_channel_runtime(PingSpikeChannelRuntime& runtime) noexcept {
    channel_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
std::int32_t __fastcall ping_spike_send_datagram_hook(void* channel, void*, void* datagram) {
#else
std::int32_t ping_spike_send_datagram_hook(void* channel, void*, void* datagram) {
#endif
    return channel_runtime->send_datagram(channel, datagram);
}
} //namespace skeet::misc
