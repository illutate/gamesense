#include "skeet/lagcomp/entity_preparation.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t subtract(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) - std::uint32_t(second));
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
float normalize(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (absolute(value) > 180.0f) {
        const float correction = round_half_away_from_zero(absolute(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value;
}
std::int32_t target_index(void* target) {
    return engine::invoke<std::int32_t>(static_cast<std::byte*>(target) + 8, 10);
}
bool after_local_player(void* target, void* engine) {
    const auto index = target_index(target);
    return engine::invoke<std::int32_t>(engine, 12) < index;
}
}
std::uint32_t prepare_entity_angles(client::PlayerStateCache& players, void* target,
                                    const LagRecord& source, bool copy_yaw,
                                    const EntityPreparationContext& context) {
    auto& cached = players.get(std::uint32_t(target_index(target)))->native;
    const auto& offsets = context.entity.offsets;
    write(target, offsets.field_432b1f5b, read<std::uint32_t>(&cached, 0x12C));
    write(target, offsets.field_0d560fac, read<std::uint32_t>(&cached, 0x148));
    write(target, offsets.eye_angles, read<std::uint32_t>(&cached, 0x1A8));
    if (copy_yaw)
        write(target, offsets.eye_angles + 4, read<std::uint32_t>(&cached, 0x1AC));
    if (source.header.collision_max_z != read<float>(&cached, 0x130)) {
        write(target, offsets.field_432b1f5b,
              read<float>(&cached, 0x144) + read<float>(&cached, 0x130));
        write(target, offsets.field_0d560fac, context.frame_value());
    }
    const auto flags = cached.flags;
    const float reference = read<float>(&cached, 0xF0);
    if (!(flags & 0x30U) && reference != std::numeric_limits<float>::max() &&
        read<float>(&cached, 0x51C) < 0.75f &&
        absolute(read<float>(&cached, 0xF4) - context.comparison_value()) < 5.0f &&
        normalize(read<float>(&cached, 0x1A8)) > 45.0f &&
        absolute(normalize(read<float>(&cached, 0x190) - cached.reference_yaw)) > 45.0f) {
        const float body_distance = absolute(normalize(reference - read<float>(&cached, 0x190)));
        const float eye_distance = absolute(normalize(reference - cached.reference_yaw));
        if (body_distance < eye_distance && eye_distance > 60.0f) {
            if (read<std::uint8_t>(&cached, 0xA8) & 1U)
                write(target, offsets.eye_angles + 4, normalize(reference + 90.0f));
            write(&cached, 0xF4, context.current_value());
            return 1;
        }
    }
    const auto count = cached.field_238;
    if (count < 2)
        return 2;
    const auto first = read<std::uint32_t>(&cached, 0x234);
    const auto sample_offset = [&](std::uint32_t ordinal) {
        return 0x23Cu + ((first + ordinal) % 6U) * 0x20U;
    };
    auto step = subtract(read<std::int32_t>(&cached, sample_offset(0)),
                         read<std::int32_t>(&cached, sample_offset(1)));
    if (step < 1)
        step = 1;
    if (step > 16)
        step = 16;
    const auto* timing = context.current_timing;
    auto now = [&] {
        if (timing) {
            const auto current = resolve_history_tick_timing(*timing);
            return add(current.tick_d4, current.tick_18c);
        }
        return add(mean_history_timing_ticks(context.latency_values()),
                   corrected_command_tick(context.entity.command_tick,
                                          context.entity.previous_command,
                                          context.entity.current_command));
    }();
    const auto next = add(read<std::int32_t>(&cached, 0x168), step);
    if (now < next)
        return 3;
    if (now == next && after_local_player(target, context.entity.engine))
        return 4;
    if (flags & 0x20U) {
        write(target, offsets.eye_angles, read<std::uint32_t>(&cached, 0x18C));
        write(target, offsets.eye_angles + 4, read<std::uint32_t>(&cached, 0x190));
        return 5;
    }
    float variation = 0.0f;
    for (std::uint32_t ordinal = 1; ordinal < count; ++ordinal) {
        const float difference = read<float>(&cached, sample_offset(ordinal - 1) + 0x1C) -
                                 read<float>(&cached, sample_offset(ordinal) + 0x1C);
        variation += absolute(normalize(difference));
    }
    if (variation <= 60.0f)
        return 6;
    std::array<float, 6> angles{};
    std::uint32_t angle_first = 0, angle_count = 0;
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
        angle_first = (angle_first + 5) % 6;
        angles[angle_first] = normalize(read<float>(&cached, sample_offset(ordinal) + 0x1C));
        ++angle_count;
        if (angle_count == 6)
            break;
    }
    std::uint32_t selected = 0;
    float minimum = std::numeric_limits<float>::max();
    for (std::uint32_t ordinal = 0; ordinal < angle_count - 1; ++ordinal) {
        const float distance =
            absolute(normalize(angles[(angle_first + ordinal) % 6] - cached.reference_yaw));
        if (distance < minimum) {
            minimum = distance;
            selected = ordinal;
        }
    }
    if (after_local_player(target, context.entity.engine))
        now = add(now, -1);
    for (auto tick = next; tick <= now; tick = add(tick, step)) {
        if (tick == now && after_local_player(target, context.entity.engine))
            break;
        ++selected;
    }
    write(target, offsets.eye_angles + 4, angles[(selected % angle_count + angle_first) % 6]);
    return 7;
}
} // namespace skeet::lagcomp
