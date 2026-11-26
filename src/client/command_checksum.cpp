#include "skeet/client/command_checksum.hpp"
#include <array>
#include <stdexcept>

namespace skeet::client {
namespace {
constexpr auto crc_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto value = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1u) ? 0xEDB88320u : 0u);
        table[i] = value;
    }
    return table;
}
constexpr auto table = crc_table();
struct Field {
    std::uint32_t offset, size;
};
// Actual successive EDI lengths / LEA ESI,[EBX+offset] in307260..308465.
constexpr std::array fields{Field{4, 4},    Field{8, 4},    Field{0xC, 0xC}, Field{0x18, 0xC},
                            Field{0x24, 4}, Field{0x28, 4}, Field{0x2C, 4},  Field{0x30, 4},
                            Field{0x34, 1}, Field{0x38, 4}, Field{0x3C, 4},  Field{0x40, 4},
                            Field{0x44, 2}, Field{0x46, 2}};
#if defined(_M_IX86) || defined(__i386__)
std::uint32_t __thiscall checksum_entry(void* command) noexcept {
#else
std::uint32_t checksum_entry(void* command) noexcept {
#endif
    return source_command_checksum(*static_cast<const Command*>(command));
}
}
std::uint32_t source_command_checksum(const Command& command) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&command);
    std::uint32_t crc = UINT32_MAX;
    for (const auto field : fields)
        for (std::uint32_t i = 0; i < field.size; ++i)
            crc = (crc >> 8) ^ table[(crc ^ bytes[field.offset + i]) & 255u];
    return ~crc;
}
CommandChecksum source_command_checksum_entry() noexcept {
    return checksum_entry;
}
void initialize_source_command_checksum(engine::NativeDataImage& data) {
    auto& cell = data.native_cell<CommandChecksum>(0x43467C48u);
    if (cell && cell != checksum_entry)
        throw std::invalid_argument("Command checksum has a conflicting publication");
    cell = checksum_entry;
}
} //namespace skeet::client
