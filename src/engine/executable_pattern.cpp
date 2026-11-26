#include "skeet/engine/executable_pattern.hpp"
#include "skeet/engine/netvars.hpp"
#include <cstring>

namespace skeet::engine {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
std::byte* executable_pattern(std::byte* module, const std::uint8_t* pattern, std::uint32_t length,
                              std::uint32_t occurrence, std::uint8_t wildcard) {
    if (!module || field<std::uint16_t>(module, 0) != 0x5A4D)
        return nullptr;
    auto* nt = module + field<std::uint32_t>(module, 0x3C);
    if (field<std::uint32_t>(nt, 0) != 0x4550)
        return nullptr;
    const auto count = field<std::uint16_t>(nt, 6);
    auto* section = nt + 0x18 + field<std::uint16_t>(nt, 0x14);
    for (std::uint32_t index = 0; index < count; ++index, section += 0x28) {
        if ((field<std::uint32_t>(section, 0x24) & 0x22000000u) != 0x20000000u)
            continue;
        const auto virtual_size = field<std::uint32_t>(section, 8);
        if (virtual_size < length)
            continue;
        auto remaining = virtual_size - length;
        auto* first = module + field<std::uint32_t>(section, 0xC);
        auto* const boundary = first + remaining;
        for (std::uint32_t found_index = 0;; ++found_index) {
            auto* found = find_byte_pattern(first, remaining, pattern, length, wildcard);
            if (!found)
                break;
            if (found_index == occurrence)
                return found;
            first = found + length;
            remaining = static_cast<std::uint32_t>(boundary - first);
        }
    }
    return nullptr;
}
} //namespace skeet::engine
