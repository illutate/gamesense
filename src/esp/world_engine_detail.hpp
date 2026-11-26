#pragma once
#include "skeet/esp/world_engine.hpp"
#include "detail.hpp"
#include <cmath>
#include <cstring>

namespace skeet::esp::world_detail {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
inline std::int32_t convar_integer(const void* object) {
    for (;;) {
        auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<std::int32_t>(
        field<std::uint32_t>(object, 0x30) ^
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)));
}
inline float squared(Vector3 a, Vector3 b) {
    const auto x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return (y * y + x * x) + z * z;
}
inline void distance(ui::RenderQueue& queue, Point at, float squared, std::uint32_t color) {
    auto text = detail::decimal(
        detail::mul(detail::sub(2, detail::truncate(std::sqrt(squared) / -12.0f)) / 5, 5));
    text += u" FT";
    queue.text(at.x, at.y, color, 0x14, 0, text);
}
} // namespace skeet::esp::world_detail
