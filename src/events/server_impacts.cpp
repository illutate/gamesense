#include "skeet/events/server_impacts.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <immintrin.h>

namespace skeet::events {
void process_server_impact(ServerImpactRuntime& runtime, GameEventReader& event,
                           EventPlayerIndices& players) {
    if (!runtime.settings.bullet_impacts.value || runtime.bindings.suspended)
        return;
    FieldValue user, coordinate;
    if (event.read(0x8361150E, FieldType::integer, user))
        return;
    std::array<float, 3> position;
    constexpr std::array<std::uint32_t, 3> fields{0x6FC60D76, 0x6EC61305, 0x6DC60E5C};
    for (std::size_t axis = 0; axis != position.size(); ++axis) {
        coordinate = FieldValue{};
        if (event.read(fields[axis], FieldType::floating, coordinate))
            return;
        position[axis] = coordinate.floating;
    }
    const auto index = players.player_for_user_id(user.integer);
    if (index != players.local_player_index())
        return;
    const std::array<float, 3> minimum{-1.0f, -1.0f, -1.0f};
    const std::array<float, 3> maximum{1.0f, 1.0f, 1.0f}, rotation{};
    // NativeCVTDQ2PS follows MXCSR rather than a host scalar conversion.
    const auto duration =
        _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(runtime.settings.impact_duration.value)));
    engine::invoke<void>(runtime.bindings.debug_overlay, 1, &position, &minimum, &maximum,
                         &rotation, std::int32_t(0), std::int32_t(0), std::int32_t(255),
                         std::int32_t(127), duration);
}
} // namespace skeet::events
