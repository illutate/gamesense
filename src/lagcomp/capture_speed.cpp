#include "skeet/lagcomp/capture_speed.hpp"
#include <cmath>
#include <cstring>

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
bool zero_integer(void* object) noexcept {
    for (;;) {
        auto* parent = read<void*>(object, 0x1C);
        if (object == parent)
            break;
        object = parent;
    }
    return read<std::uint32_t>(object, 0x30) ==
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
}
bool apply_history_capture_speed_limit(const RecordRing& history, void* player,
                                       const HistoryCaptureSpeedBindings& bindings) {
    if (!history.count)
        return false;
    const auto flags = read<std::uint8_t>(player, bindings.flags_e2fbe695);
    if (!(flags & 1u))
        return false;
    const bool grounded_before = (history.at(0).header.player_flags & 1u) != 0;
    float limit;
    if (grounded_before) {
        const auto maximum = engine::invoke<float>(player, 276);
        limit = minimum_number(read<float>(player, bindings.maximum_speed_3eb71843), maximum);
        if (read<std::uint8_t>(player, bindings.mode_7090d8dd))
            limit *= 0.5199999809265137f;
        else if (read<std::uint8_t>(player, bindings.flags_e2fbe695) & 2u) {
            const auto duck = read<float>(player, bindings.entity.offsets.duck_amount);
            limit *= (duck * 0.3400000035762787f + 1.0f) - duck;
        }
    } else {
        if (!zero_integer(bindings.convar_4b52a9bf))
            return false;
        limit = read<float>(player, bindings.maximum_speed_3eb71843) * 1.100000023841858f;
        if (!(limit > 0.0f))
            return false;
    }
    const auto velocity = read<Vector>(player, bindings.entity.offsets.velocity);
    const auto speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    if (!(speed > limit))
        return false;
    const auto scale = limit / speed;
    write(player, bindings.entity.offsets.velocity, velocity[0] * scale);
    write(player, bindings.entity.offsets.velocity + 4, velocity[1] * scale);
    return true;
}
} // namespace skeet::lagcomp
