#include "skeet/lagcomp/angle_publication.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (std::fabs(value) > 180.0f) {
        const float correction = round_half_away_from_zero(std::fabs(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value;
}
}
std::uint32_t history_angle_entry_index(std::uint16_t capacity, std::uint16_t first,
                                        std::int32_t ordinal) noexcept {
    const auto index = std::uint32_t(first) + std::uint32_t(ordinal);
    return std::bit_cast<std::int32_t>(index) < std::int32_t(capacity) ? index : index - capacity;
}
void publish_history_final_angles(void* player, const Vector& previous,
                                  const EntityStateRuntime& entity, std::uint32_t state_offset) {
    const auto pitch = normalize(read<float>(player, entity.offsets.eye_angles));
    const auto clamped = _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(pitch), _mm_set_ss(-90.0f)), _mm_set_ss(90.0f)));
    write(player, entity.offsets.eye_angles, clamped);
    if (clamped == previous[0] && read<float>(player, entity.offsets.eye_angles + 4) == previous[1])
        return;
    auto* state = static_cast<std::byte*>(player) + state_offset;
    if (read<std::uint16_t>(state, 0x10)) {
        const auto index = history_angle_entry_index(read<std::uint16_t>(state, 0xC),
                                                     read<std::uint16_t>(state, 0xE), 0);
        auto* entry = read<std::byte*>(state, 8) + std::size_t(index) * 16u;
        write(entry, 4, read<std::uint32_t>(player, entity.offsets.eye_angles));
        write(entry, 8, read<std::uint32_t>(player, entity.offsets.eye_angles + 4));
    }
    if (auto* target = read<std::byte*>(state, 0x14)) {
        write(target, 0, read<std::uint32_t>(player, entity.offsets.eye_angles));
        target = read<std::byte*>(state, 0x14);
        write(target, 4, read<std::uint32_t>(player, entity.offsets.eye_angles + 4));
    }
}
} // namespace skeet::lagcomp
