#include "skeet/misc/clan_tag.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::misc {
namespace {
template <class T> T read(void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<std::byte*>(object) + offset, sizeof(value));
    return value;
}
std::int32_t signed_bits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}
}
std::int32_t ClanTagClock::mean_latency_ticks() const {
    return lagcomp::mean_history_timing_ticks(latency);
}
std::int32_t ClanTagClock::corrected_client_tick() const {
    auto result = client_tick;
    if (acknowledged_tick && outgoing_tick) {
        const auto difference = std::uint32_t(*outgoing_tick) - std::uint32_t(*acknowledged_tick);
        if (difference - 1 < 9)
            result = signed_bits(std::uint32_t(result) + difference);
    }
    return result;
}
void ClanTagRuntime::reset() noexcept {
    was_enabled = 0;
}
void ClanTagRuntime::update() {
    if (!set_tag)
        return;
    if (!*enabled) {
        if (!was_enabled)
            return;
        was_enabled = 0;
        if (!engine::invoke<bool>(engine, 26) || !clan_id_cvar ||
            !read<std::uint32_t>(clan_id_cvar, 0x60))
            return;
        const auto callbacks = read<void**>(clan_id_cvar, 0x54);
        using Callback = void (*)(void*, const char*, float);
        reinterpret_cast<Callback>(callbacks[0])(static_cast<std::byte*>(clan_id_cvar) + 0x18, "",
                                                 0.0f);
        return;
    }
    const auto tick = signed_bits(std::uint32_t(clock->mean_latency_ticks()) +
                                  std::uint32_t(clock->corrected_client_tick()));
    std::int32_t frame;
    auto* rules = game_rules ? *game_rules : nullptr;
    const auto phase = rules ? read<std::int32_t>(rules, game_phase_offset) : 0;
    if (phase == 4 || phase == 5)
        frame = 11;
    else {
        const auto period = _mm_cvttss_si32(_mm_set_ss(0.3f / clock->interval_per_tick + 0.5f));
        const auto step = std::uint32_t(tick / period) % 30;
        static constexpr std::array<std::int32_t, 30> frames{
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 11, 11, 11,
            11, 11, 11, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};
        frame = frames[step];
    }
    if (frame == last_frame)
        return;
    last_frame = frame;
    std::array<char, 44> text{};
    constexpr char animation[] = "             gamesense                ";
    std::memcpy(text.data(), animation, sizeof(animation));
    changed = 1;
    auto* tag = text.data() + frame;
    tag[15] = '\0';
    set_tag(tag, tag);
    was_enabled = 1;
    changed = 0; // native final WORD store at class +0x28
}
} // namespace skeet::misc
