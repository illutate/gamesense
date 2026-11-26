#include "skeet/lagcomp/velocity_validation.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
}

bool validate_velocity_component(float& value, float maximum) noexcept {
    const auto component = _mm_load_ss(&value);
    // The original UCOMISS self-test is quiet for a quiet NaN. CMPUNORDSS has
    // the same NaN/denormal handling, including the signaling-NaN exception.
    if (_mm_movemask_ps(_mm_cmpunord_ss(component, component)) & 1) {
        value = 0.0f;
        return false;
    }

    const auto upper = _mm_set_ss(maximum);
    if (_mm_comigt_ss(component, upper)) {
        _mm_store_ss(&value, upper);
        return false;
    }

    // XORPS at 433E4FA5 flips the sign, including signed zero, infinity and
    // NaN payloads. Neither comparison accepts an unordered operand as greater.
    const auto lower = _mm_xor_ps(upper, _mm_set_ss(-0.0f));
    if (_mm_comigt_ss(lower, component)) {
        _mm_store_ss(&value, lower);
        return false;
    }
    return true;
}

bool validate_velocity(Vector& velocity, float maximum) noexcept {
    const bool x_valid = validate_velocity_component(velocity[0], maximum);
    const bool y_valid = validate_velocity_component(velocity[1], maximum);
    const bool z_valid = validate_velocity_component(velocity[2], maximum);
    return x_valid & y_valid & z_valid;
}

bool validate_velocity(Vector& velocity, const void* maximum_convar) noexcept {
    for (;;) {
        const auto* parent = read<const void*>(maximum_convar, 0x1C);
        if (parent == maximum_convar)
            break;
        maximum_convar = parent;
    }
    const auto encoded = read<std::uint32_t>(maximum_convar, 0x2C);
    const auto address = std::uint32_t(reinterpret_cast<std::uintptr_t>(maximum_convar));
    return validate_velocity(velocity, std::bit_cast<float>(encoded ^ address));
}
} // namespace skeet::lagcomp
