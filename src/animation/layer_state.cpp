#include "skeet/animation/layer_state.hpp"
#include <cstddef>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T field(const void* layer, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(layer) + offset, sizeof value);
    return value;
}
}

std::uint8_t layer_is_active(const void* layer) noexcept {
    if (field<std::int32_t>(layer, 0x04) <= 0)
        return 0;

    // ANDPS at 433CC624 clears the sign bit without converting the float.
    const auto rate_bits = field<std::uint32_t>(layer, 0x14) & 0x7fffffffu;
    float absolute_rate;
    std::memcpy(&absolute_rate, &rate_bits, sizeof absolute_rate);
    // COMISS/JBE rejects zero and unordered operands, including either NaN.
    if (!(absolute_rate > 0.0f))
        return 0;

    // 4347089C contains 3F7FFE00: 1 - 2^-15, exactly representable in float.
    // COMISS/JAE rejects ordered >=; unordered cycle values reach MOV AL,1.
    constexpr float completed_cycle = 0x1.fffcp-1f;
    return static_cast<std::uint8_t>(!(field<float>(layer, 0x18) >= completed_cycle));
}

} // namespace skeet::animation
