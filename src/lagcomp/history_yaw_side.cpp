#include "skeet/lagcomp/history_yaw_side.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::lagcomp {
namespace {
float owner_float(const client::NativePlayerStateLayout& owner, std::size_t offset) noexcept {
    float value;
    std::memcpy(&value, reinterpret_cast<const std::byte*>(&owner) + offset, sizeof value);
    return value;
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
float normalized_distance(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    //4341AD12 is the concrete recovered half-away rounding operation. The
    // initial NaN test occurs before normalization; infinity can consequently
    // become NaN during the subtraction and must remain unordered afterward.
    if (absolute(value) > 180.0f) {
        const float correction = round_half_away_from_zero(absolute(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value < 0.0f ? std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u)
                        : value;
}
}

void update_history_yaw_side(client::PlayerState& owner, bool enemy, std::uint32_t update_flags,
                             const std::uint8_t& enabled, HistoryYawSideFmod fmod) {
    if (!enemy || !enabled || (update_flags & 1u) || !owner.history.count)
        return;
    auto& state = owner.native;
    const auto& newest = owner.history.at(0).header;
    float current = state.reference_yaw;
    float previous = std::bit_cast<float>(newest.player_history_1ac);
    if (update_flags & 4u)
        current = previous;
    else if (newest.flags & 8u) {
        if (owner.history.count < 2u)
            return;
        previous = std::bit_cast<float>(owner.history.at(1).header.player_history_1ac);
    }
    // Native UCOMISS equality exits only for ordered, equal values.
    if (previous == current)
        return;

    const float first_distance = normalized_distance(current - owner_float(state, 0x3C));
    const float second_distance = normalized_distance(current - owner_float(state, 0x60));
    // Prologue:4346FF70=15,43472C88=165,4346EEB0=20,4347222C=-20.
    if (first_distance < 15.0f) {
        state.flags |= 4u;
        return;
    }
    if (second_distance < 15.0f) {
        state.flags &= ~4u;
        return;
    }

    // SUBSS precedes CVTPS2PD: preserve the single-precision difference before
    // the actual double CRT remainder and the final single-precision store.
    const float difference = current - previous;
    float delta = static_cast<float>(fmod(double(difference), 360.0));
    if (current > previous) {
        if (delta >= 180.0f)
            delta -= 360.0f;
    } else if (delta <= -180.0f)
        delta += 360.0f;

    //4340EAA1..EAAA simplifies to XOR4, including when it was already set.
    // Unordered COMISS takes the following bounded-angle arm instead.
    if (absolute(delta) > 165.0f)
        state.flags ^= 4u;
    else if (delta > 20.0f)
        state.flags |= 4u;
    else if (delta < -20.0f)
        state.flags &= ~4u;
}
} // namespace skeet::lagcomp
