#include "skeet/engine/animation_startup_patterns.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>

namespace skeet::engine {
void initialize_animation_update_flag(NativeDataImage& data) {
    const auto* code = data.native_cell<const std::byte*>(0x434693F0u);
    if (!code)
        return;
    constexpr std::array<std::uint8_t, 11> pattern{0xC6, 0x05, 0xCC, 0xCC, 0xCC, 0xCC,
                                                   0xCC, 0xF3, 0x0F, 0x5F, 0x05};
    const auto* match = find_byte_pattern(const_cast<std::byte*>(code), 512, pattern.data(),
                                          static_cast<std::uint32_t>(pattern.size()), 0xCC);
    if (!match)
        return;
    std::uint32_t address;
    std::memcpy(&address, match + 2, sizeof address);
    data.native_cell<std::uint8_t*>(0x43467B24u) =
        reinterpret_cast<std::uint8_t*>(std::uintptr_t(address));
}
} // namespace skeet::engine
