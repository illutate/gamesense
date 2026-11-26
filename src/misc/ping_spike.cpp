#include "skeet/misc/ping_spike.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <emmintrin.h>

namespace skeet::misc {
bool PingSpikeRuntime::active() const {
    return *enabled && hotkey_active(hotkey->binding, keys) && engine::invoke<bool>(engine, 26) &&
           !engine::invoke<bool>(engine, 82) && !engine::invoke<bool>(engine, 93);
}
void PingSpikeRuntime::rewind_sequence(void* channel) const {
    if (!active())
        return;
    const auto latencies = _mm_add_sd(_mm_set_sd(*latency_4347a508), _mm_set_sd(*latency_4347b120));
    const auto latency = _mm_cvtpd_ps(latencies);
    const auto scale = _mm_set_ss(0.001f);
    auto requested = _mm_mul_ss(_mm_cvtepi32_ps(_mm_cvtsi32_si128(*milliseconds)), scale);
    const auto maximum =
        _mm_mul_ss(_mm_cvtepi32_ps(_mm_cvtsi32_si128(*maximum_unlag_milliseconds)), scale);
    if (_mm_comigt_ss(_mm_add_ss(requested, latency), maximum))
        requested = _mm_sub_ss(maximum, latency);
    requested = _mm_max_ss(requested, _mm_setzero_ps());
    requested = _mm_min_ss(requested, maximum);
    const auto ticks = _mm_cvttss_si32(
        _mm_add_ss(_mm_div_ss(requested, _mm_set_ss(*interval_per_tick)), _mm_set_ss(0.5f)));
    auto* destination = static_cast<std::byte*>(channel) + 0x1C;
    std::uint32_t sequence;
    std::memcpy(&sequence, destination, sizeof(sequence));
    sequence -= std::uint32_t(ticks);
    std::memcpy(destination, &sequence, sizeof(sequence));
}
} // namespace skeet::misc
