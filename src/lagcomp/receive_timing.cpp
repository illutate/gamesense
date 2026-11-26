#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/engine/interface_call.hpp"
#include <xmmintrin.h>
#include <cstring>

namespace skeet::lagcomp {
std::int32_t sample_receive_tick(void* engine, const float& interval) {
#if defined(_M_IX86) || defined(__i386__)
    const auto received = engine::invoke<long double>(engine, 14);
#else
    const auto received = static_cast<long double>(engine::invoke<float>(engine, 14));
#endif
    const float rounded = static_cast<float>(received / static_cast<long double>(interval) + 0.5L);
    return _mm_cvtt_ss2si(_mm_set_ss(rounded));
}
std::int32_t sample_receive_tick(void* engine, std::byte* const& globals) {
#if defined(_M_IX86) || defined(__i386__)
    const auto received = engine::invoke<long double>(engine, 14);
#else
    const auto received = static_cast<long double>(engine::invoke<float>(engine, 14));
#endif
    float interval;
    std::memcpy(&interval, globals + 0x20, sizeof(interval));
    const float rounded = static_cast<float>(received / static_cast<long double>(interval) + 0.5L);
    return _mm_cvtt_ss2si(_mm_set_ss(rounded));
}
} //namespace skeet::lagcomp
