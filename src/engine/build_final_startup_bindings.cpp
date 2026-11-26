#include "skeet/engine/build_final_startup_bindings.hpp"
#include "skeet/engine/overlay_build.hpp"
#include <cstring>
#include <initializer_list>
#include <cstdio>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* bytes, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, bytes + offset, sizeof value);
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Image {
    std::byte* base;
    std::uint32_t size, text_begin, text_size;
    void code(std::uint32_t offset, std::initializer_list<int> pattern) const {
        require(offset >= text_begin && pattern.size() <= text_size &&
                    offset - text_begin <= text_size - pattern.size(),
                "Final startup instruction exceeds its module text");
        std::size_t index = 0;
        for (int value : pattern) {
            if (value >= 0 && base[offset + index] != std::byte(value)) {
                char message[192];
                std::snprintf(
                    message, sizeof message,
                    "Final startup offset mismatch: module %p + 0x%08X, expected %02X, got %02X",
                    static_cast<void*>(base), offset + static_cast<std::uint32_t>(index),
                    static_cast<unsigned>(value), std::to_integer<unsigned>(base[offset + index]));
                throw std::runtime_error(message);
            }
            ++index;
        }
    }
    bool contains(const void* pointer, std::size_t length) const {
        const auto first = reinterpret_cast<std::uintptr_t>(base),
                   address = reinterpret_cast<std::uintptr_t>(pointer);
        return address >= first && length <= size && address - first <= size - length;
    }
    const std::byte* unique(std::initializer_list<int> pattern) const {
        const std::byte* found = nullptr;
        for (std::uint32_t offset = text_begin; offset - text_begin <= text_size - pattern.size();
             ++offset) {
            std::size_t index = 0;
            bool matches = true;
            for (int value : pattern) {
                if (value >= 0 && base[offset + index] != std::byte(value)) {
                    matches = false;
                    break;
                }
                ++index;
            }
            if (matches) {
                require(!found, "Final startup instruction pattern is ambiguous");
                found = base + offset;
            }
        }
        require(found != nullptr, "A required final startup instruction pattern is absent");
        return found;
    }
};
Image checked(void* pointer, std::uint32_t nt_offset, std::uint32_t timestamp, std::uint32_t size,
              std::uint32_t checksum) {
    auto* const base = static_cast<std::byte*>(pointer);
    require(base && read<std::uint16_t>(base) == 0x5A4D &&
                read<std::uint32_t>(base, 0x3C) == nt_offset,
            "Unsupported final startup DOS/NT layout");
    const auto* const nt = base + nt_offset;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt, 4) == 0x14C &&
                read<std::uint16_t>(nt, 0x14) == 0xE0 && read<std::uint16_t>(nt, 0x18) == 0x10B &&
                read<std::uint32_t>(nt, 8) == timestamp && read<std::uint32_t>(nt, 0x50) == size &&
                read<std::uint32_t>(nt, 0x58) == checksum,
            "Unsupported final startup module build");
    Image result{base, size, 0, 0};
    unsigned found = 0;
    const auto sections = read<std::uint16_t>(nt, 6);
    require(sections && sections <= 16, "Final startup PE section count is invalid");
    for (unsigned index = 0; index < sections; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        if (!std::memcmp(section, ".text\0\0\0", 8)) {
            result.text_begin = read<std::uint32_t>(section, 12);
            result.text_size = read<std::uint32_t>(section, 8);
            require(result.text_size && result.text_begin < size &&
                        result.text_size <= size - result.text_begin &&
                        (read<std::uint32_t>(section, 36) & 0x60000000u) == 0x60000000u,
                    "Final startup text extent or protection differs");
            ++found;
        }
    }
    require(found == 1, "Final startup module has no unique executable text section");
    return result;
}
Image checked_overlay(void* pointer) {
    const auto identity = require_overlay_build_identity(static_cast<const std::byte*>(pointer));
    return checked(pointer, 0x130, identity.timestamp, 0x190000, identity.checksum);
}
}
BuildFinalStartupBindings initialize_build_final_startup_bindings(NativeDataImage& data) {
    const auto client =
        checked(data.native_cell<void*>(0x4346A55Cu), 0x150, 0x65272025, 0x5509000, 0xFA635F);
    const auto engine =
        checked(data.native_cell<void*>(0x43468D20u), 0x150, 0x64EE7E86, 0x39C0000, 0x64174E);
    const auto overlay = checked_overlay(data.native_cell<void*>(0x43468D08u));
    client.code(0x3DC9B0, {0x55, 0x8B, 0xEC, 0x8B, 0x55, 8,    0x83, 0xEC, 0x1C, 0xF6,
                           0x42, 0x1C, 1,    0x0F, 0x84, 0x85, 0,    0,    0,    0x8B,
                           0x4A, 0x1C, 0x8B, 0xC1, 0xD1, 0xE8, 0xA8, 1,    0x74, 0x7A});
    client.code(0x3DCA1D, {0x8D, 0x88, -1, -1, -1, -1, 0xF3, 0x0F, 0x11, 0x45, 0xE4});
    const auto feedback = read<std::uint32_t>(client.base + 0x3DCA1F);
    require(feedback >= 0x14 && feedback < 0x20000,
            "Final player feedback field is outside the supported entity layout");
    // The real decoder uses a36-byte record selected from its type word. The
    // final float callback is its field+8 in the first record at52B268.
    engine.code(0x1FFDC4, {0x8B, 0x42, 8, 0x56, 0x52, 0x8D, 4, 0xC0, 0x8B, 4, 0x85, -1, -1, -1, -1,
                           0xFF, 0xD0});
    const auto table = read<std::byte*>(engine.base + 0x1FFDCF);
    require(table == engine.base + 0x52B268, "Final float decoder table operand differs");
    engine.code(0x1FD950,
                {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 8, 0x8B, 0x56, 0x34, 0x8B, 0x4E, 0x30, 0xE8});
    auto** const float_slot = reinterpret_cast<void**>(table + 8);
    const auto* const returned =
        overlay.unique({0x3D, -1, -1,   -1, -1,   0x73, 0x1A, 0x68, -1,   -1,   -1,   -1,
                        0xE8, -1, -1,   -1, -1,   0x8B, 0x0D, -1,   -1,   -1,   -1,   0x83,
                        0xC4, 4,  0x8B, 1,  0x6A, 0,    0xFF, 0x50, 0x14, 0x3B, 0x7B, 0x3C});
    auto** const stream = read<void**>(returned + 19);
    const auto* const stored = overlay.unique({0x89, 0x1D, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x83});
    const auto* const matrix = read<const std::byte*>(stored + 2);
    require(overlay.contains(stream, sizeof(void*)) && overlay.contains(matrix, sizeof(void*)),
            "Final overlay operands do not refer to their actual module storage");
    data.native_cell<const std::byte*>(0x4346D540u) = client.base + 0x3DC9B0;
    data.native_cell<void**>(0x43476A40u) = float_slot;
    data.native_cell<const std::byte*>(0x43468D94u) = returned;
    data.native_cell<const std::byte*>(0x43468350u) = matrix;
    // These two static masks precede the actual final XOR publication. They
    // are native data, not retained instructions or captured entity pointers.
    data.native_cell<std::uint32_t>(0x434689D0u) = 0x74108329u ^ (feedback - 0x14u);
    data.native_cell<std::uint32_t>(0x434691E0u) = 0x0C5D8DD5u ^ feedback;
    return {client.base + 0x3DCA1D, feedback, float_slot, returned, stream};
}
} //namespace skeet::engine
