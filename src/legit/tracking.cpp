#include "skeet/legit/tracking.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/client/player_state.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <stdexcept>

namespace skeet::legit {
namespace {
std::int32_t index(void* entity) {
    const auto value = engine::invoke<std::int32_t>(static_cast<std::byte*>(entity) + 8, 10);
    if (value == 0)
        throw std::invalid_argument("LEGIT target requires a nonzero native networkable index");
    return value;
}
std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t ticks(float seconds, float interval) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(seconds / interval + 0.5f));
}
}
Vector estimate_target_velocity(const ProfileState& state, const TargetTrackingBindings& bindings,
                                void* entity) {
    const auto id = std::uint32_t(index(entity));
    const auto& ring =
        bindings.players ? bindings.players->get(id)->history : bindings.histories[id & 63u];
    if (ring.count && state.reaction_time != 0.0f) {
        const auto interval = bindings.live_interval ? *bindings.live_interval : bindings.interval;
        const auto clock =
            bindings.live_clock_tick ? *bindings.live_clock_tick : bindings.clock_tick;
        const auto latest = subtract(clock, ticks(state.reaction_time, interval));
        const auto width = maximum_number(0.02f, state.reaction_time * 0.5f);
        const auto earliest = subtract(latest, ticks(width, interval));
        Vector sum{};
        std::int32_t count = 0;
        for (std::uint32_t ordinal = 0; ordinal != ring.count; ++ordinal) {
            const auto& record = ring.at(ordinal).header;
            if (record.command_tick > latest)
                continue;
            if (record.command_tick < earliest)
                break;
            for (unsigned axis = 0; axis != 3; ++axis)
                sum[axis] += record.velocity[axis];
            ++count;
        }
        if (count) {
            const auto inverse = 1.0f / float(count);
            for (auto& component : sum)
                component *= inverse;
            return sum;
        }
    }
    Vector result;
    std::memcpy(result.data(), static_cast<std::byte*>(entity) + bindings.velocity_offset,
                sizeof(result));
    return result;
}
void predict_target_point(const ProfileState& state, const TargetTrackingBindings& bindings,
                          void* entity, Vector& point) {
    auto velocity = estimate_target_velocity(state, bindings, entity);
    velocity[2] *= 0.0001f;
    auto& track = bindings.tracks[std::uint32_t(index(entity)) & 63u];
    if (track.velocity_valid) {
        const Vector delta{velocity[0] - track.velocity[0], velocity[1] - track.velocity[1],
                           velocity[2] - track.velocity[2]};
        const auto length =
            std::sqrt((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2]);
        const auto inverse = 1.0f / (length + 1.1920928955078125e-7f);
        const auto step =
            (bindings.live_interval ? *bindings.live_interval : bindings.interval) * 1500.0f;
        if (!(length > step)) {
            if (-step > length)
                for (unsigned axis = 0; axis != 3; ++axis)
                    velocity[axis] = track.velocity[axis] - (inverse * delta[axis]) * step;
        } else
            for (unsigned axis = 0; axis != 3; ++axis)
                velocity[axis] = (inverse * delta[axis]) * step + track.velocity[axis];
    }
    if (!track.velocity_valid)
        track.velocity_valid = 1;
    track.velocity = velocity;
    const auto speed = std::sqrt((velocity[0] * velocity[0] + velocity[1] * velocity[1]) +
                                 velocity[2] * velocity[2]);
    if (!(0.1f > speed) && !(speed > 320.0f))
        for (unsigned axis = 0; axis != 3; ++axis)
            point[axis] = track.interval * velocity[axis] + point[axis];
}
} // namespace skeet::legit
