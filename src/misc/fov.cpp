#include "skeet/misc/fov.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <emmintrin.h>

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
void write_override(void* context, float value) {
    std::memcpy(static_cast<std::byte*>(context) + 8, &value, sizeof value);
}
}
void ZoomSensitivityRuntime::reset() {
    if (const auto context = bindings.override_context)
        write_override(context, 0.0f);
}
void ZoomSensitivityRuntime::update() {
    const auto initial_context = bindings.override_context;
    if (!initial_context)
        return;
    write_override(initial_context, 0.0f);
    auto* player = bindings.local_player;
    if (!player || settings.zoom_fov.value == 100 ||
        read<std::int32_t>(player, bindings.fov_offset) == 45)
        return;
    if (settings.zoom_fov.value == 0) {
        write_override(initial_context, 1.0f);
        return;
    }
    auto current = _mm_set_ss(engine::invoke<float>(player, 332));
    auto base = engine::invoke<std::int32_t>(player, 333);
    const auto current_integer = _mm_cvttss_si32(current);
    if (current_integer == base)
        return;
    if (!base)
        base = 1;
    const auto difference = signed_bits(std::uint32_t(current_integer) - std::uint32_t(base));
    if (difference) {
        auto scaled = _mm_cvtepi32_ps(_mm_cvtsi32_si128(settings.zoom_fov.value));
        scaled = _mm_mul_ss(scaled, _mm_set_ss(0.01f));
        scaled = _mm_mul_ss(scaled, _mm_cvtepi32_ps(_mm_cvtsi32_si128(difference)));
        scaled = _mm_add_ss(scaled, _mm_set_ss(0.5f));
        const auto interpolated =
            signed_bits(std::uint32_t(_mm_cvttss_si32(scaled)) + std::uint32_t(base));
        current = _mm_cvtepi32_ps(_mm_cvtsi32_si128(interpolated));
    }
    auto* parent = reinterpret_cast<void*>(
        std::uintptr_t(bindings.encoded_mouse_sensitivity_cvar ^ 0x1EEEF408u));
    for (;;) {
        auto* next = read<void*>(parent, 0x1C);
        if (next == parent)
            break;
        parent = next;
    }
    const auto encoded =
        read<std::uint32_t>(parent, 0x2C) ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(parent));
    auto ratio = _mm_div_ss(current, _mm_cvtepi32_ps(_mm_cvtsi32_si128(base)));
    ratio = _mm_mul_ss(ratio, _mm_set_ss(std::bit_cast<float>(encoded)));
    // Native reloads43468994 after both player virtuals; they may replace it.
    write_override(bindings.override_context, _mm_cvtss_f32(ratio));
}
} // namespace skeet::misc
