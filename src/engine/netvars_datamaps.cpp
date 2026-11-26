#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>
#include <limits>

namespace skeet::engine {
namespace {
bool has_min_length_4(const char* name) noexcept {
    return name && name[0] && name[1] && name[2] && name[3];
}
} // namespace

std::uint32_t find_data_map_offset(DataMap& map, std::uint32_t table_hash,
                                   std::uint32_t property_hash) {
    const bool match_table = netvar_name_hash(map.class_name) == table_hash;
    for (std::int32_t i = 0; i < map.field_count; ++i) {
        const auto& field = map.fields[i];
        if (!has_min_length_4(field.name))
            continue;
        if (match_table && netvar_name_hash(field.name) == property_hash)
            return field.offset;
        if (field.embedded && field.embedded->class_name) {
            const auto result = find_data_map_offset(*field.embedded, table_hash, property_hash);
            if (result != std::numeric_limits<std::uint32_t>::max())
                return result;
        }
    }
    if (map.base && map.base->class_name)
        return find_data_map_offset(*map.base, table_hash, property_hash);
    return std::numeric_limits<std::uint32_t>::max();
}

std::byte* find_byte_pattern(std::byte* first, std::uint32_t size, const std::uint8_t* pattern,
                             std::uint32_t length, std::uint8_t wildcard) {
    if (!first || length == 0 || size < length || !pattern)
        return nullptr;
    const auto last = length - 1;
    auto wildcard_position = last;
    while (wildcard_position && pattern[wildcard_position] != wildcard)
        --wildcard_position;
    auto distance = static_cast<std::uint8_t>(last - wildcard_position);
    if (distance == 0)
        distance = 1;
    std::array<std::uint8_t, 256> shifts;
    shifts.fill(distance);
    for (auto index = last - distance; index < last; ++index)
        shifts[pattern[index]] = static_cast<std::uint8_t>(last - index);
    // The native helper clamps the address addition to UINT32_MAX. Host-side
    // tests have wider addresses; valid ranges cannot overflow their address.
    const auto remaining = static_cast<std::uintptr_t>(~reinterpret_cast<std::uintptr_t>(first));
    const auto effective_size = static_cast<std::uint32_t>(remaining < size ? remaining : size);
    const auto end = first + effective_size - length;
    while (first <= end) {
        std::int32_t index = static_cast<std::int32_t>(last);
        while (index >= 0) {
            const auto expected = pattern[index];
            if (expected != wildcard && std::to_integer<std::uint8_t>(first[index]) != expected)
                break;
            if (index == 0)
                return first;
            --index;
        }
        first += shifts[std::to_integer<std::uint8_t>(first[last])];
    }
    return nullptr;
}

DataMap* find_data_map(std::byte* client_module, std::uint32_t class_hash) {
    if (!client_module)
        return nullptr;
    static constexpr std::array<std::uint8_t, 29> pattern{
        0xcc, 0xcc, 0xcc, 0xcc, 0xc7, 0x05, 0, 0, 0, 0,    0,    0,    0,    0,   0xc7,
        0x05, 0,    0,    0,    0,    0,    0, 0, 0, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc};
    std::uint32_t nt_offset, image_size;
    std::memcpy(&nt_offset, client_module + 0x3c, 4);
    std::memcpy(&image_size, client_module + nt_offset + 0x50, 4);
    auto* end = client_module + image_size - pattern.size();
    auto* first = client_module;
    for (unsigned i = 0; i != 43; ++i) {
        auto* found =
            find_byte_pattern(first, static_cast<std::uint32_t>(end - first), pattern.data(),
                              static_cast<std::uint32_t>(pattern.size()), 0);
        if (!found)
            return nullptr;
        DataMap* map;
        std::memcpy(&map, found + 16, sizeof(map));
        if (map && netvar_name_hash(map->class_name) == class_hash)
            return map;
        first = found + pattern.size();
    }
    return nullptr;
}

void bind_data_map_offset(const DataMapOffsetBinding& binding, std::uint32_t*& cursor) {
    if (!binding.map)
        return;
    *binding.pointer_output = cursor;
    const auto offset =
        find_data_map_offset(*binding.map, binding.table_hash, binding.property_hash);
    *binding.xor_key ^= *cursor ^ offset;
    ++cursor;
}

} // namespace skeet::engine
