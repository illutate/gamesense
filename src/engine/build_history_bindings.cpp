#include "skeet/engine/build_history_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* address) {
    T value;
    std::memcpy(&value, address, sizeof value);
    return value;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Section {
    const char* name;
    std::uint32_t rva, size, flags;
};
constexpr std::array<Section, 3> sections{{{".text\0\0", 0x1000, 0xB38409, 0x60000020},
                                           {".rdata\0", 0xB3A000, 0x26629C, 0x40000040},
                                           {".data\0\0", 0xDA1000, 0x460C3E0, 0xC0000040}}};
struct Image {
    std::byte* base;
    std::byte* code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(rva >= 0x1000 && pattern.size() <= sections[0].size &&
                    rva - 0x1000 <= sections[0].size - pattern.size(),
                "History function leaves client executable text");
        auto* const result = base + rva;
        auto* cursor = result;
        for (const int expected : pattern) {
            require(expected < 0 || std::uint8_t(*cursor) == expected,
                    "Unsupported history engine instruction");
            ++cursor;
        }
        return result;
    }
    void call(std::uint32_t rva, std::uint32_t target) const {
        code(rva, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(rva) + 5 + read<std::int32_t>(base + rva + 1) == target,
                "History engine function has a different direct callee");
    }
    void string_operand(std::uint32_t operand, std::uint32_t rva) const {
        require(rva >= sections[1].rva && rva - sections[1].rva < sections[1].size,
                "History string operand is outside client read-only data");
        require(read<std::uint32_t>(base + operand) == reinterpret_cast<std::uintptr_t>(base + rva),
                "History string operand has different client provenance");
    }
};
Image checked(void* module) {
    require(module, "History binding requires the initialized client.dll module");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported history engine DOS header");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Unsupported history engine client build");
    std::array<bool, 3> found{};
    for (unsigned index = 0; index < 7; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto size = read<std::uint32_t>(section + 8), rva = read<std::uint32_t>(section + 12);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva,
                "History client section exceeds image");
        for (unsigned entry = 0; entry < sections.size(); ++entry) {
            const auto& expected = sections[entry];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[entry] && size == expected.size && rva == expected.rva &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "History client section profile differs");
            found[entry] = true;
        }
    }
    require(found[0] && found[1] && found[2], "History client section is missing");
    return {base};
}
BuildHistoryBindings acquire(const Image& image) {
    auto* const name =
        image.code(0x1D9440, {0x55, 0x8B, 0xEC, 0x83, 0x7D, 8,  0xFF, 0x56, 0x8B, 0xF1, 0x75,
                              0x0A, 0xB8, -1,   -1,   -1,   -1, 0x5E, 0x5D, 0xC2, 4,    0,
                              0x83, 0xBE, 0x50, 0x29, 0,    0,  0,    0x75, 0x14});
    image.string_operand(0x1D944D, 0xB96700);
    image.call(0x1D946E, 0x1CF260);
    image.call(0x1D9485, 0x19D7D0);
    image.code(0x1D9473, {0x8B, 0x8E, 0x50, 0x29, 0, 0, 0x85, 0xC9, 0x74, 0x12, 0x83, 0x39, 0, 0x74,
                          0x0D, 0x8B, 0x55, 8});
    image.code(0x1D948A, {0x5E, 0x5D, 0xC2, 4, 0, 0xB8, -1, -1, -1, -1, 0x5E, 0x5D, 0xC2, 4, 0});
    image.string_operand(0x1D9490, 0xB966F4);
    auto* const activity =
        image.code(0x1D94A0, {0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 8,    0x56, 0x8B, 0xF1,
                              0x83, 0xFB, 0xFF, 0x0F, 0x84, 0x8E, 0,    0,    0,    0x83,
                              0xBE, 0x50, 0x29, 0,    0,    0,    0x75, 0x14});
    image.call(0x1D94CB, 0x1CF260);
    image.call(0x1D94E1, 0x36B3D0);
    image.call(0x1D9516, 0x36B600);
    image.call(0x1D952A, 0x19CD60);
    image.code(0x1D952F,
               {0x8B, 0x47, 0x10, 0x5F, 0x5E, 0x5B, 0x5D, 0xC2, 4,    0,    0x5E, 0x33, 0xC0, 0x5B,
                0x5D, 0xC2, 4,    0,    0x5E, 0x83, 0xC8, 0xFF, 0x5B, 0x5D, 0xC2, 4,    0});
    auto* const displacement = image.code(
        0x19D600, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0xFA,
                   0x85, 0xF6, 0x75, 0x14, 0x68, -1,   -1,   -1,   -1,   0xFF, 0x15, -1,
                   -1,   -1,   -1,   0x83, 0xC4, 4,    0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    image.string_operand(0x19D611, 0xB8EA10);
    image.call(0x19D624, 0x36B3D0);
    image.call(0x19D65B, 0x7E0370);
    image.code(0x19D64D,
               {0x8D, 0x45, 0xF4, 0x8B, 0xD7, 0x50, 0xFF, 0x75, 0x0C, 0x8B, 0xCE, 0xFF, 0x75, 8});
    image.code(0x19D660, {0x83, 0xC4, 0x0C, 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    image.code(0x19D6C4, {0x8B, 0x45, 0x0C, 0xC7, 0, 0, 0, 0, 0,    0xC7, 0x40, 4,    0,    0,   0,
                          0,    0xC7, 0x40, 8,    0, 0, 0, 0, 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    return {reinterpret_cast<MemberFunction<std::int32_t, std::int32_t>>(activity),
            reinterpret_cast<MemberFunction<const char*, std::int32_t>>(name), displacement};
}
}
BuildHistoryBindings initialize_build_history_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "History engine bindings require the complete canonical x86 data image");
    const auto result = acquire(checked(data.native_cell<void*>(0x4346A55Cu)));
    data.native_cell<decltype(result.activity)>(0x4346A364u) = result.activity;
    data.native_cell<decltype(result.sequence_name)>(0x434678C8u) = result.sequence_name;
    data.native_cell<void*>(0x43468DC0u) = result.sequence_displacement;
    return result;
}
} //namespace skeet::engine
