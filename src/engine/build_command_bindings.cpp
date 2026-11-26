#include "skeet/engine/build_command_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
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
void require(bool value, const char* message) {
    if (!value)
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
    bool contains(std::uint32_t rva, std::size_t size, unsigned kind) const noexcept {
        const auto& s = sections[kind];
        return rva >= s.rva && size <= s.size && rva - s.rva <= s.size - size;
    }
    std::byte* code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(contains(rva, pattern.size(), 0), "Command binding instruction leaves client text");
        auto* const output = base + rva;
        auto* cursor = output;
        for (const int value : pattern) {
            require(value < 0 || std::uint8_t(*cursor) == value,
                    "Unsupported command engine instruction");
            ++cursor;
        }
        return output;
    }
    std::byte* operand(std::uint32_t at, std::uint32_t target, std::size_t size,
                       unsigned kind) const {
        require(contains(at, 4, 0) && contains(target, size, kind),
                "Command operand has wrong section provenance");
        require(read<std::uint32_t>(base + at) == reinterpret_cast<std::uintptr_t>(base + target),
                "Command operand has a different module target");
        return base + target;
    }
    void call(std::uint32_t at, std::uint32_t target) const {
        code(at, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(at) + 5 + read<std::int32_t>(base + at + 1) == target,
                "Command engine call reaches a different function");
    }
    void pointer(std::uint32_t at, std::uint32_t target, std::size_t size, unsigned kind) const {
        require(contains(at, 4, 1) && contains(target, size, kind),
                "Command vtable/RTTI target has wrong provenance");
        require(read<std::uint32_t>(base + at) == reinterpret_cast<std::uintptr_t>(base + target),
                "Command vtable/RTTI identity differs");
    }
};
Image checked(void* module) {
    require(module != nullptr, "Command bindings require the initialized client.dll module");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported command client DOS header");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Unsupported command client build");
    std::array<bool, 3> found{};
    for (unsigned index = 0; index < 7; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto size = read<std::uint32_t>(section + 8), rva = read<std::uint32_t>(section + 12);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva, "Command PE section exceeds image");
        for (unsigned kind = 0; kind < sections.size(); ++kind) {
            const auto& expected = sections[kind];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[kind] && size == expected.size && rva == expected.rva &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Command PE section profile differs");
            found[kind] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Command client section is missing");
    return {base};
}
BuildCommandBindings acquire(const Image& client) {
    auto* const seed =
        client.code(0x9A6740, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x70, 0x6A, 0x58,
                               0x8D, 0x44, 0x24, 0x1C, 0x89, 0x4C, 0x24, 8,    0x6A, 0,    0x50});
    client.call(0x9A6756, 0xACB6E0);
    client.code(0x9A675B,
                {0x6A, 4, 0x8D, 0x54, 0x24, 0x14, 0xC7, 0x44, 0x24, 0x28, 1, 0x23, 0x45, 0x67});
    client.call(0x9A6795, 0x9A65E0);
    client.call(0x9A67A5, 0x9A66B0);
    client.code(0x9A679A, {0x83, 0xC4, 0x10, 0x8D, 0x54, 0x24, 0x18, 0x8D, 0x4C, 0x24, 8});
    client.code(0x9A67AA, {0x8B, 0x44, 0x24, 0x0E, 0x8B, 0xE5, 0x5D, 0xC3});
    // This real engine caller agrees with the source master's ECX command
    // number and its31-bit mask/store. No cdecl seed thunk is assumed.
    client.code(0x3774CE, {0x8B, 0x4F, 4});
    client.call(0x3774D1, 0x9A6740);
    client.code(0x3774D6, {0x25, 0xFF, 0xFF, 0xFF, 0x7F, 0x89, 0x47, 0x40});

    client.code(0x2EFA0, {0x68, -1, -1, -1, -1, 0xC7, 5, -1, -1, -1, -1, -1, -1, -1, -1});
    client.operand(0x2EFA1, 0xB19470, 1, 0);
    client.operand(0x2EFA7, 0x5334CC0, 4, 2);
    auto* const helper = client.operand(0x2EFAB, 0xDF5B30, 0x1C, 2);
    client.code(0xB19470, {0xB9, -1, -1,   -1, -1, 0xC7, 5,  -1, -1, -1, -1, -1, -1,
                           -1,   -1, 0xC7, 5,  -1, -1,   -1, -1, 0,  0,  0,  0});
    client.operand(0xB19471, 0xDF5B34, 0x18, 2);
    client.operand(0xB19477, 0xDF5B30, 0x1C, 2);
    client.operand(0xB1947B, 0xBDC908, 12, 1);
    client.operand(0xB19481, 0x5334CC0, 4, 2);
    require(read<std::uint32_t>(helper) == reinterpret_cast<std::uintptr_t>(client.base + 0xBDC908),
            "Actual CMoveHelperClient object is not initialized with its proven vtable");
    client.pointer(0xBDC904, 0xD593A4, 20, 1);
    client.pointer(0xD593B0, 0xE217B8, 32, 2);
    constexpr char name[] = ".?AVCMoveHelperClient@@";
    require(read<std::uint32_t>(client.base + 0xD593A8) == 0 &&
                std::memcmp(client.base + 0xE217C0, name, sizeof name) == 0,
            "Command move-helper RTTI differs");
    client.pointer(0xBDC90C, 0x31F9C0, 0x12, 0);
    client.pointer(0xBDC910, 0x31F9F0, 8, 0);
    client.code(0x31F9C0, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 8, 0x89, 0x41, 0x18, 0x8B, 1, 0xFF, 0x50,
                           8, 0x5D, 0xC2, 4, 0});
    client.code(0x31F9F0, {0xC7, 0x41, 0x10, 0, 0, 0, 0, 0xC3});

    // Real prediction RunCommand sets and resets the two shared static words.
    client.code(0x343C1A, {0x85, 0xFF, 0x75, 0x0C, 0xC7, 5,    -1,   -1,   -1, -1, 0xFF, 0xFF,
                           0xFF, 0xFF, 0xEB, 8,    0x8B, 0x47, 0x40, 0xA3, -1, -1, -1,   -1});
    auto* const random_seed = client.operand(0x343C20, 0xDAC254, 4, 2);
    client.operand(0x343C2E, 0xDAC254, 4, 2);
    client.code(0x343C42, {0x89, 0x35, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x48, 0x20});
    auto* const player = client.operand(0x343C44, 0x53323C0, 4, 2);
    client.code(0x343E87, {0xC7, 5, -1, -1, -1, -1, 0xFF, 0xFF, 0xFF, 0xFF,
                           0xC7, 5, -1, -1, -1, -1, 0,    0,    0,    0});
    client.operand(0x343E89, 0xDAC254, 4, 2);
    client.operand(0x343E93, 0x53323C0, 4, 2);

    client.code(0x25D00, {0xA1, -1, -1,   -1, -1, 0x68, -1, -1, -1, -1, 0xA3, -1, -1,
                          -1,   -1, 0xC7, 5,  -1, -1,   -1, -1, -1, -1, -1,   -1});
    auto* const flags = client.operand(0x25D01, 0x5334DA4, 4, 2);
    client.operand(0x25D11, 0x5334DA4, 4, 2);
    client.operand(0x25D0B, 0xDEF48C, 4, 2);
    client.operand(0x25D15, 0xDEF488, 12, 2);
    client.code(0x680C0, {0xA1, -1, -1,   -1, -1, 0x68, -1, -1, -1, -1, 0xA3, -1, -1,
                          -1,   -1, 0xC7, 5,  -1, -1,   -1, -1, -1, -1, -1,   -1});
    client.operand(0x680C1, 0x5334DA4, 4, 2);
    client.operand(0x680D1, 0x5334DA4, 4, 2);
    // Both real prediction branches push slot/frame and call the same
    // RestoreEntityToPredictedFrame body. It restores each eligible predicted
    // entity through the genuine paired engine operations and returns RET8.
    client.code(0x344767, {0x80, 0x7C, 0x1E, 0x48, 0, 0x75, 0x26, 0x48, 0x50, 0x57});
    client.call(0x344771, 0x344610);
    client.code(0x344A28, {0x8D, 0x41, 0xFF, 0x50, 0x57});
    client.call(0x344A2D, 0x344610);
    client.code(0x344610, {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 4,  0x56, 0xE8, -1,   -1,   -1,   -1,
                           0x8B, 0x75, 8,    0xA1, -1,   -1, -1,   -1,   0x83, 0xFE, 0xFF, 0x74,
                           7,    0x8B, 4,    0xB5, -1,   -1, -1,   -1,   0x85, 0xC0, 0x74, 0x5F});
    client.call(0x344617, 0x7CD410);
    client.operand(0x344620, 0x52BF6D8, 8, 2);
    client.operand(0x34462C, 0x52BF6D8, 8, 2);
    client.code(0x344660, {0x8B, 4,    0x9D, -1,   -1,   -1,   -1,   0x8B, 0x3C, 0xB0, 0x85,
                           0xFF, 0x74, 0x1D, 0x80, 0xBF, 0xEE, 2,    0,    0,    0,    0x74,
                           0x14, 0x6A, 2,    0xFF, 0x75, 0x0C, 0x51, 0x8B, 0xCF});
    client.operand(0x344663, 0x4DF45FC, 4, 2);
    client.call(0x34467F, 0x1EC950);
    client.code(0x344684, {0x8B, 0xCF});
    client.call(0x344686, 0x1ECA30);
    client.code(0x34468B, {0x46, 0x3B, 0x75, 8, 0x7C, 0xCF, 0x5F, 0x5B, 0x5E, 0x5D, 0xC2, 8, 0});
    return {reinterpret_cast<prediction::SeedGenerator>(seed), helper,
            reinterpret_cast<std::int32_t*>(random_seed), reinterpret_cast<void**>(player),
            reinterpret_cast<prediction::PredictionFlagNode**>(flags)};
}
}
BuildCommandBindings initialize_build_command_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Command engine binding requires complete canonical x86 source data");
    const auto result = acquire(checked(data.native_cell<void*>(0x4346A55Cu)));
    data.native_cell<prediction::SeedGenerator>(0x434679ACu) = result.seed_generator;
    data.native_cell<void*>(0x43468EC8u) = result.move_helper;
    data.native_cell<std::int32_t*>(0x43468328u) = result.random_seed;
    data.native_cell<void**>(0x434693A4u) = result.prediction_player;
    data.native_cell<prediction::PredictionFlagNode**>(0x43467C58u) = result.flag_head;
    data.native_cell<MemberFunction<void, std::int32_t, std::int32_t>>(0x4346D660u) =
        reinterpret_cast<MemberFunction<void, std::int32_t, std::int32_t>>(
            data.native_cell<std::byte*>(0x4346A55Cu) + 0x344610);
    return result;
}
} //namespace skeet::engine
