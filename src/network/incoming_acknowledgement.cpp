#include "skeet/network/incoming_update.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
std::int32_t engine_tick(IncomingUpdateGlobals& g) {
    const auto time = engine::invoke<float>(g.engine_client, 14);
    const auto interval = field<float>(g.engine_globals, 0x20);
    return _mm_cvtt_ss2si(_mm_set_ss(float(static_cast<long double>(time) / interval + 0.5L)));
}
std::int32_t history_tag(IncomingUpdateGlobals& g, std::int32_t command) { //43340A83
    const auto* state =
        prediction::CommandHistoryView::from_owner(g.prediction_owner).find(command);
    return state ? field<std::int32_t>(state, 0) : -1;
}
}
void reconcile_incoming_acknowledgement(void* player, bool& changed, IncomingUpdateGlobals& g,
                                        const IncomingUpdateBindings& b) {
    if (player && !engine::invoke<bool>(g.engine_client, 82) &&
        !engine::invoke<bool>(g.engine_client, 93) && *g.sequence > 0) {
        const auto tick_now = engine_tick(g);
        if (tick_now != *g.sequence)
            *g.acknowledged = *g.old_acknowledged;
        else {
            const auto command = engine::invoke<std::int32_t>(g.engine_client, 205);
            if (command != g.previous_command) {
                if (g.transition_status == 2 || g.transition_status == 1) {
                    *g.acknowledged = *g.old_acknowledged;
                    changed = true;
                }
            } else if (g.transition_status == 2) {
                const auto processed = engine::invoke<std::int32_t>(g.engine_client, 205);
                if (processed == g.previous_processed_command) {
                    const auto tick = field<std::int32_t>(player, b.tick_base_3d10b94c);
                    //433F9367 makes this call separately before its150-step loop.
                    (void)engine::invoke<std::int32_t>(g.engine_client, 205);
                    for (std::uint32_t age = 0; age < 150; ++age) {
                        const auto current = engine::invoke<std::int32_t>(g.engine_client, 205);
                        const auto candidate =
                            std::bit_cast<std::int32_t>(std::uint32_t(current) - age);
                        if (history_tag(g, candidate) == tick) {
                            *g.acknowledged = candidate;
                            break;
                        }
                    }
                }
            }
        }
    }
    g.previous_command = -1;
}
} //namespace skeet::network
