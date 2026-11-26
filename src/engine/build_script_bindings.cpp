#include "skeet/engine/build_script_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* address) {
    T result;
    std::memcpy(&result, address, sizeof result);
    return result;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Image {
    std::byte* base;
    std::uint32_t text_size, rdata_begin, rdata_size, data_begin, data_size;
    bool contains(std::uint32_t address, std::size_t size, std::uint32_t begin,
                  std::uint32_t length) const {
        const auto first = reinterpret_cast<std::uintptr_t>(base) + begin;
        return address >= first && size <= length && address - first <= length - size;
    }
    void code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(rva >= 0x1000u && pattern.size() <= text_size &&
                    rva - 0x1000u <= text_size - pattern.size(),
                "Script binding code exceeds its module text");
        std::size_t index = 0;
        for (const auto value : pattern) {
            require(value < 0 || static_cast<unsigned char>(base[rva + index]) == value,
                    "Script binding instruction fingerprint differs");
            ++index;
        }
    }
    std::byte* operand(std::uint32_t rva, std::uint32_t target) const {
        require(read<std::uint32_t>(base + rva) == reinterpret_cast<std::uintptr_t>(base + target),
                "Script binding operand has different module provenance");
        return base + target;
    }
};
Image checked_image(void* module, bool client) {
    require(module != nullptr, "Script bindings require the initialized client/engine modules");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Script binding module DOS/NT-header layout differs");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 20) == 0xE0 &&
                read<std::uint16_t>(nt + 24) == 0x10B &&
                read<std::uint32_t>(nt + 8) == (client ? 0x65272025u : 0x64EE7E86u) &&
                read<std::uint32_t>(nt + 80) == (client ? 0x5509000u : 0x39C0000u) &&
                read<std::uint32_t>(nt + 88) == (client ? 0xFA635Fu : 0x64174Eu),
            "Script binding module build identity differs");
    const Image image{base,
                      client ? 0xB38409u : 0x451C32u,
                      client ? 0xB3A000u : 0x453000u,
                      client ? 0x26629Cu : 0xD4766u,
                      client ? 0xDA1000u : 0x528000u,
                      client ? 0x460C3E0u : 0x3427638u};
    struct Section {
        char name[8];
        std::uint32_t rva, size, flags;
    };
    const std::array<Section, 3> expected{
        {{".text", 0x1000, image.text_size, 0x60000020},
         {".rdata", image.rdata_begin, image.rdata_size, 0x40000040},
         {".data", image.data_begin, image.data_size, 0xC0000040}}};
    for (const auto& wanted : expected) {
        unsigned matches = 0;
        for (unsigned index = 0; index < 7; ++index) {
            const auto* section = nt + 0xF8 + index * 40;
            if (std::memcmp(section, wanted.name, 8))
                continue;
            require(read<std::uint32_t>(section + 12) == wanted.rva &&
                        read<std::uint32_t>(section + 8) == wanted.size &&
                        read<std::uint32_t>(section + 36) == wanted.flags,
                    "Script binding module section differs");
            ++matches;
        }
        require(matches == 1, "Script binding module section is missing or duplicated");
    }
    return image;
}
}
BuildScriptBindings initialize_build_script_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Script module bindings require the original x86 ABI");
    const auto engine = checked_image(data.native_cell<void*>(0x43468D20u), false);
    const auto client = checked_image(data.native_cell<void*>(0x4346A55Cu), true);
    // GetCommandAck reads the same raw split-screen record used by the other
    // client-state aliases. The public current-state pointer is raw+8.
    engine.code(0xB6830, {0xA1, -1, -1, -1, -1, 0x8B, 0x80, 0x3C, 0x4D, 0, 0, 0xC3});
    const auto record = read<std::uint32_t>(engine.operand(0xB6831, 0x59F19C));
    require(record && !(record & 3u) &&
                record <= std::numeric_limits<std::uint32_t>::max() - 0x4D40u &&
                data.native_cell<void*>(0x43468E44u) ==
                    reinterpret_cast<void*>(std::uintptr_t(record + 8u)),
            "Script globals do not share the canonical current client state");
    engine.code(0x286110, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 8,    0x89, 0x81, 0x14, 1,    0, 0, 0x8B,
                           0x45, 0x0C, 0x89, 0x81, 0x34, 0x4D, 0,    0,    0x5D, 0xC2, 8, 0});
    engine.code(0x286146, {0x8B, 0x86, 0x34, 0x4D, 0,    0,    0x8B, 0xD0, 0x2B, 0x96,
                           0x2C, 0x4D, 0,    0,    0x89, 0x8E, 0x30, 0x4D, 0,    0});
    engine.code(0xD779C, {0x8B, 0x86, 0x3C, 0x4D, 0,    0,    0xFF, 0xB6, 0x1C, 1,    0,    0,
                          0x8B, 0x11, 0x2B, 0x86, 0x34, 0x4D, 0,    0,    0x50, 0xFF, 0x52, 0x10});

    // The inline CClassMap object is initialized by this actual constructor.
    // Its fourth virtual compares registered class names and returns size.
    client.code(0x26ACF0, {0xC7, 5, -1, -1, -1, -1,   -1, -1, -1, -1, 0xB8, -1, -1, -1, -1,
                           0xC7, 5, -1, -1, -1, -1,   0,  0,  0,  0,  0xC7, 5,  -1, -1, -1,
                           -1,   0, 0,  0,  0,  0xC7, 5,  -1, -1, -1, -1,   0,  0,  0,  0});
    auto* const classes = client.operand(0x26ACF2, 0x535E4CC);
    auto* const table = client.operand(0x26ACF6, 0xBC162C);
    client.operand(0x26ACFB, 0x535E4CC);
    client.operand(0x26AD01, 0x535E4D4);
    client.operand(0x26AD0B, 0x535E4D8);
    client.operand(0x26AD15, 0x535E4DC);
    require(read<std::uint32_t>(classes) == reinterpret_cast<std::uintptr_t>(table) &&
                client.contains(reinterpret_cast<std::uint32_t>(table), 5 * sizeof(void*),
                                client.rdata_begin, client.rdata_size),
            "Script class-size object has a different live vtable");
    client.operand(0xBC163C, 0x26AF90);
    client.code(0x26AF90, {0x55, 0x8B, 0xEC, 0x51, 0x53, 0x0F, 0xB7, 0x59, 0x16, 0x56, 0x33, 0xF6,
                           0x57, 0x85, 0xDB, 0x7E, 0x51, 0x8B, 0x51, 8,    0x89, 0x55, 0xFC});
    client.code(0x26AFF2, {0x5F, 0x5E, 0x83, 0xC8, 0xFF, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 4, 0,
                           0x8B, 0x47, 4,    0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 4, 0});
    client.operand(0xBC1628, 0xD53B3C);
    const auto* const locator = client.base + 0xD53B3C;
    require(read<std::uint32_t>(locator) == 0 && read<std::uint32_t>(locator + 4) == 0 &&
                read<std::uint32_t>(locator + 8) == 0,
            "Script class-map RTTI primary locator differs");
    auto* const type = client.operand(0xD53B48, 0xE1EBD8);
    require(std::memcmp(type + 8, ".?AVCClassMap@@", 16) == 0,
            "Script class-map RTTI name differs");
    const BuildScriptBindings result{
        data, reinterpret_cast<const std::int32_t*>(std::uintptr_t(record + 0x4D34u)),
        reinterpret_cast<const std::int32_t*>(std::uintptr_t(record + 0x4D3Cu)), classes};
    data.native_cell<const std::int32_t*>(0x43475794u) = result.old_command_ack;
    data.native_cell<const std::int32_t*>(0x43475BA4u) = result.command_ack;
    data.native_cell<void*>(0x43469304u) = result.class_size_interface;
    return result;
}
}
