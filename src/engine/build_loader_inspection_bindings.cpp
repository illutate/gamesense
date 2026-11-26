#include "skeet/engine/build_loader_inspection_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const void* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
struct Section {
    char name[8];
    std::uint32_t rva, size, flags;
};
struct Profile {
    std::uint32_t nt, timestamp, size, checksum;
    std::uint16_t section_count;
    std::array<Section, 3> sections;
};
constexpr Profile engine_build{0x150,
                               0x64EE7E86,
                               0x39C0000,
                               0x64174E,
                               7,
                               {{{".text", 0x1000, 0x451C32, 0x60000020},
                                 {".rdata", 0x453000, 0xD4766, 0x40000040},
                                 {".data", 0x528000, 0x3427638, 0xC0000040}}}};
constexpr Profile material_build{0x118,
                                 0x63DC3DE4,
                                 0x114000,
                                 0xDE784,
                                 6,
                                 {{{".text", 0x1000, 0x91D46, 0x60000020},
                                   {".rdata", 0x93000, 0x28074, 0x40000040},
                                   {".data", 0xBC000, 0x42480, 0xC0000040}}}};
struct Image {
    std::byte* base;
    const Profile& profile;
    bool within(const void* pointer, std::size_t size, std::size_t section) const noexcept {
        const auto& region = profile.sections[section];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto first = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= first && size <= region.size && address - first <= region.size - size;
    }
};
Image checked(void* module, const Profile& profile) {
    require(module != nullptr, "Required loader-inspection module is not published");
    auto* base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == profile.nt,
            "Loader-inspection DOS/NT layout differs");
    const auto* nt = base + profile.nt;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == profile.section_count &&
                read<std::uint16_t>(nt + 0x14) == 0xE0 && read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == profile.timestamp &&
                read<std::uint32_t>(nt + 0x50) == profile.size &&
                read<std::uint32_t>(nt + 0x58) == profile.checksum,
            "Unsupported loader-inspection PE32 build");
    std::array<bool, 3> found{};
    for (std::size_t i = 0; i < profile.section_count; ++i) {
        const auto* section = nt + 0xF8 + i * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= profile.size && size <= profile.size - rva,
                "Loader-inspection section exceeds image");
        for (std::size_t j = 0; j < found.size(); ++j) {
            const auto& expected = profile.sections[j];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[j] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Loader-inspection section fingerprint differs");
            found[j] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Required loader-inspection section is missing");
    return {base, profile};
}
bool matches(const std::byte* code, std::initializer_list<int> pattern) noexcept {
    for (const auto expected : pattern) {
        if (expected >= 0 && static_cast<unsigned char>(*code) != expected)
            return false;
        ++code;
    }
    return true;
}
void expect(const Image& image, const std::byte* code, std::initializer_list<int> pattern) {
    require(image.within(code, pattern.size(), 0) && matches(code, pattern),
            "Loader-inspection instruction fingerprint differs");
}
const std::byte* unique(const Image& image, std::initializer_list<int> pattern, std::uint32_t rva) {
    const auto& text = image.profile.sections[0];
    const std::byte* found = nullptr;
    for (std::size_t i = 0; i <= text.size - pattern.size(); ++i) {
        const auto* code = image.base + text.rva + i;
        if (!matches(code, pattern))
            continue;
        require(!found, "Loader-inspection code anchor is ambiguous");
        found = code;
    }
    require(found == image.base + rva, "Loader-inspection code anchor is missing or unsupported");
    return found;
}
const std::byte* absolute(const Image& image, const std::byte* operand, std::size_t size,
                          std::size_t section) {
    const auto* pointer =
        reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(operand)));
    require(image.within(pointer, size, section),
            "Loader-inspection operand has invalid section provenance");
    return pointer;
}
const std::byte* relative(const Image& image, const std::byte* instruction, std::uint8_t opcode,
                          std::uint32_t rva) {
    require(image.within(instruction, 5, 0) && static_cast<std::uint8_t>(*instruction) == opcode,
            "Loader-inspection relative transfer differs");
    const auto offset =
        std::int64_t(instruction - image.base) + 5 + read<std::int32_t>(instruction + 1);
    require(offset == rva && offset >= 0 && offset < image.profile.size,
            "Loader-inspection transfer target differs");
    const auto* target = image.base + static_cast<std::size_t>(offset);
    require(image.within(target, 1, 0), "Loader-inspection transfer leaves executable text");
    return target;
}
}

void initialize_build_loader_inspection_inputs(NativeDataImage& data) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Loader inspection requires the complete canonical x86 data image");
    const auto engine = checked(data.native_cell<void*>(0x43468D20u), engine_build);
    const auto material = checked(data.native_cell<void*>(0x4346A600u), material_build);
    auto* const receiver = data.native_cell<void*>(0x4346A514u);
    require(engine.within(receiver, sizeof(void*), 2),
            "Loader inspection requires the actual VEngineClient014 receiver");
    const auto* table = read<const std::byte*>(receiver);
    const auto* state_method = read<const std::byte*>(table + 222 * sizeof(void*));
    require(state_method == engine.base + 0xB7550, "Engine inspection virtual222 differs");
    expect(engine, state_method,
           {0x8B, 0x0D, -1, -1, -1, -1, 0x8D, 0x89, 0x60, 0x4E, 0, 0, 0xE9, -1, -1, -1, -1});
    absolute(engine, state_method + 2, 4, 2);
    const auto* state_target = relative(engine, state_method + 0xC, 0xE9, 0xB93A0);
    expect(engine, state_target,
           {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x1C, 0x8B, 0xD1, 0x83, 0x7A, 0x20, 0,
            0x8D, 0x4A, 0x20});

    const auto* clock_block =
        unique(engine, {0x2B, 0x05, -1,   -1,   -1,   -1,   0x03, 0x05, -1,   -1,   -1,
                        -1,   0x66, 0x0F, 0x6E, 0xD8, 0x33, 0xC0, 0x0F, 0x5B, 0xDB, 0x85,
                        0xDB, 0x0F, 0x44, 0xC8, 0x51, 0x50, 0xF3, 0x0F, 0x59, 0x1D, -1,
                        -1,   -1,   -1,   0x8B, 0xCE, 0xE8, -1,   -1,   -1,   -1},
               0x1A3656);
    const auto* first_counter = absolute(engine, clock_block + 2, 4, 2);
    require(absolute(engine, clock_block + 8, 4, 2) == first_counter + 0x14,
            "Engine inspection counter operands disagree");
    absolute(engine, clock_block + 0x20, 4, 2);
    const auto* clock_target = relative(engine, clock_block + 0x26, 0xE8, 0x1A6090);
    expect(engine, clock_target,
           {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x60, 0x56, 0x8B, 0xF1, 0xF3, 0x0F, 0x11, 0x5D, 0xFC});

    const auto* material_loop =
        unique(material, {0x8B, 0x7D, 0xE8, 0x33, 0xF6, 0x85, 0xFF, 0x7E, 0x24, 0x0F, 0x1F, 0,
                          0x83, 0x3C, 0xB3, 0,    0x75, 0x16, 0x8B, 0x4D, 0x08, 0x56, 0x8B, 0x01,
                          0xFF, 0x50, 0x0C, 0x8B, 0x4D, 0xF0, 0x8B, 0x10, 0xE8, -1,   -1,   -1,
                          -1,   0x89, 0x04, 0xB3, 0x46, 0x3B, 0xF7, 0x7C, 0xDF},
               0xBBD4);
    const auto* material_call = material_loop + 0x20;
    const auto* material_target = relative(material, material_call, 0xE8, 0x24B10);
    expect(material, material_target,
           {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, -1,   -1,   -1,   -1,  0x64,
            0xA1, 0,    0,    0,    0,    0x50, 0x64, 0x89, 0x25, 0,   0,
            0,    0,    0x51, 0x53, 0x56, 0x57, 0x8B, 0xFA, 0x8B, 0xD9});
    absolute(material, material_target + 6, 1, 0);

    const auto* tree_walk =
        unique(engine, {0x56, 0x8B, 0x35, -1,   -1,   -1,   -1,   0x57, 0x8B, 0x3D, -1,   -1,
                        -1,   -1,   0x66, 0x90, 0x83, 0xFE, 0xFF, 0x74, 0x3D, 0x6B, 0xC6, 0x6C,
                        0x8B, 0x04, 0x38, 0x83, 0xF8, 0xFF, 0x74, 0x04, 0x8B, 0xF0, 0xEB, 0xEC},
               0xDA360);
    const auto* tree_header = absolute(engine, tree_walk + 0xA, 0x1C, 2);
    require(tree_header == engine.base + 0x59E9CC &&
                absolute(engine, tree_walk + 3, 4, 2) == tree_header + 0xC,
            "Engine inspection tree operands disagree");

    data.native_cell<const std::byte*>(0x4346D4D0u) = state_method;
    data.native_cell<const std::byte*>(0x4346D4C8u) = material_call;
    data.native_cell<const std::byte*>(0x4346D4C4u) = clock_block;
    data.native_cell<const std::byte*>(0x4346D4C0u) = tree_header;
}
} // namespace skeet::engine
