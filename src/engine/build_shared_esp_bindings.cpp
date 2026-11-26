#include "skeet/engine/build_shared_esp_bindings.hpp"
#include "skeet/engine/rtti.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof value);
    return value;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Region {
    std::uint32_t rva, size, flags;
};
constexpr Region text{0x1000, 0x451C32, 0x60000020}, rdata{0x453000, 0xD4766, 0x40000040},
    objects{0x528000, 0x3427638, 0xC0000040};
struct Engine {
    std::byte* base;
    bool contains(const void* pointer, std::size_t size, Region region) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer),
                   first = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= first && size <= region.size && address - first <= region.size - size;
    }
    void expect(const std::byte* code, std::initializer_list<int> pattern) const {
        require(contains(code, pattern.size(), text),
                "Shared ESP instruction is outside genuine engine text");
        std::size_t index = 0;
        for (const auto value : pattern) {
            require(value < 0 || static_cast<unsigned char>(code[index]) == value,
                    "Shared ESP engine instruction fingerprint differs");
            ++index;
        }
    }
    const std::byte* unique(std::initializer_list<int> pattern) const {
        const std::byte* result = nullptr;
        for (std::uint32_t offset = 0; offset <= text.size - pattern.size(); ++offset) {
            const auto* code = base + text.rva + offset;
            bool match = true;
            std::size_t index = 0;
            for (const auto value : pattern) {
                if (value >= 0 && static_cast<unsigned char>(code[index]) != value) {
                    match = false;
                    break;
                }
                ++index;
            }
            if (!match)
                continue;
            require(!result, "Shared ESP engine CALL anchor is ambiguous");
            result = code;
        }
        require(result != nullptr, "Shared ESP engine CALL anchor is missing");
        return result;
    }
    const std::byte* call(const std::byte* instruction) const {
        expect(instruction, {0xE8, -1, -1, -1, -1});
        const auto* result = instruction + 5 + read<std::int32_t>(instruction + 1);
        require(contains(result, 1, text), "Shared ESP CALL target leaves genuine engine text");
        return result;
    }
};
Engine checked(NativeDataImage& data, NativeImageEnvironment images) {
    auto* module = data.native_cell<std::byte*>(0x43468D20);
    require(module && images.image_nt_header,
            "Shared ESP requires the loaded engine and PE-header service");
    const auto* nt = images.image_nt_header(module);
    require(nt == module + 0x150 && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x150 && read<std::uint32_t>(nt) == 0x4550 &&
                read<std::uint16_t>(nt + 4) == 0x14C && read<std::uint16_t>(nt + 6) == 7 &&
                read<std::uint16_t>(nt + 20) == 0xE0 && read<std::uint16_t>(nt + 24) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x64EE7E86 &&
                read<std::uint32_t>(nt + 80) == 0x39C0000 &&
                read<std::uint32_t>(nt + 88) == 0x64174E,
            "Unsupported Shared ESP engine PE32 build");
    constexpr std::array<Region, 3> regions{text, rdata, objects};
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        require(read<std::uint32_t>(section + 12) == regions[index].rva &&
                    read<std::uint32_t>(section + 8) == regions[index].size &&
                    read<std::uint32_t>(section + 36) == regions[index].flags,
                "Shared ESP engine section geometry differs");
    }
    return {module};
}
void require_class(const Engine& engine, const std::byte* table, const char* name) {
    require(engine.contains(table - 4, 4, rdata) && engine.contains(table, 4, rdata),
            "Shared ESP engine message table is outside read-only data");
    const auto* locator = read<const std::byte*>(table - 4);
    require(engine.contains(locator, 20, rdata) && read<std::uint32_t>(locator) == 0 &&
                read<std::uint32_t>(locator + 4) == 0 && read<std::uint32_t>(locator + 8) == 0,
            "Shared ESP engine message RTTI locator is invalid");
    const auto* type = read<const std::byte*>(locator + 12);
    require(engine.contains(type, 8 + std::strlen(name) + 1, objects) &&
                !std::strcmp(reinterpret_cast<const char*>(type + 8), name),
            "Shared ESP engine message RTTI identity differs");
}
}
void initialize_build_shared_esp_bindings(NativeDataImage& data, NativeImageEnvironment images) {
    const auto engine = checked(data, images);
    const auto* message = engine.unique({0x8D, 0x4C, 0x24, 0x18, 0xE8, -1, -1, -1, -1,
                                         0x56, 0x8D, 0x84, 0x24, 0xCC, 1,  0,  0,  0x50,
                                         0x8D, 0x4C, 0x24, 0x28, 0xE8, -1, -1, -1, -1}) +
                          4;
    const auto* construct = engine.call(message);
    const auto* destroy = engine.call(message + 0xB2);
    engine.expect(construct,
                  {0x56, 0x57, 0x8B, 0xF9, 0x8D, 0x4F, 8,    0xC7, 7,    -1,   -1,   -1,
                   -1,   0xE8, -1,   -1,   -1,   -1,   0xC7, 7,    -1,   -1,   -1,   -1,
                   0x8B, 0xC7, 0xC7, 0x47, 8,    -1,   -1,   -1,   -1,   0xC6, 0x47, 0x38,
                   0,    0xC7, 0x47, 0x50, 0x0F, 0,    0,    0,    0xC7, 0x47, 0x4C, 0,
                   0,    0,    0,    0xC6, 0x47, 0x3C, 0,    0x5F, 0x5E, 0xC3});
    engine.expect(destroy, {0x53, 0x8B, 0xD9, 0x56, 0x8D, 0x73, 0x3C, 0x57, 0xC7, 3, -1,
                            -1,   -1,   -1,   0xC7, 0x43, 8,    -1,   -1,   -1,   -1});
    engine.expect(destroy + 0x72, {0xC7, 3, -1, -1, -1, -1, 0x5B, 0xC3});
    const auto* voice_table = read<const std::byte*>(construct + 0x14);
    require(voice_table == read<const std::byte*>(destroy + 0xA) &&
                read<const std::byte*>(construct + 0x1D) == read<const std::byte*>(destroy + 0x11),
            "Shared ESP voice constructor/destructor tables disagree");
    require_class(engine, voice_table, ".?AV?$CNetMessagePB@$09VCCLCMsg_VoiceData@@$08$0A@@@");
    (void)engine.call(construct + 0xD);

    const auto* receiver = engine.unique({0x8D, 0x4D, 0xCC, 0xE8, -1,   -1, -1,   -1,   0x8B,
                                          0xD6, 0x8D, 0x4D, 0xD0, 0xE8, -1, -1,   -1,   -1,
                                          0x8B, 0x8F, 0x94, 0,    0,    0,  0x8D, 0x55, 0xCC}) +
                           3;
    construct = engine.call(receiver);
    destroy = engine.call(receiver + 0x28);
    engine.expect(construct,
                  {0x56, 0x8B, 0xF1, 0xC7, 6,    -1,   -1,   -1,   -1,   0x8D, 0x4E, 8,
                   0xC7, 0x46, 4,    -1,   -1,   -1,   -1,   0xE8, -1,   -1,   -1,   -1,
                   0xC7, 0x46, 0x10, 0,    0,    0,    0,    0x8B, 0xC6, 0xC7, 0x46, 0x0C,
                   -1,   -1,   -1,   -1,   0xC7, 0x46, 0x14, 0,    0,    0,    0,    0xC7,
                   6,    -1,   -1,   -1,   -1,   0xC7, 0x46, 4,    -1,   -1,   -1,   -1,
                   0xC6, 0x46, 0x18, 1,    0xC7, 0x46, 0x30, 0x0F, 0,    0,    0,    0xC7,
                   0x46, 0x2C, 0,    0,    0,    0,    0xC6, 0x46, 0x1C, 0,    0x5E, 0xC3});
    engine.expect(destroy, {0x53, 0x8B, 0xD9, 0x56, 0x8D, 0x73, 0x1C, 0x57, 0xC7, 3, -1,
                            -1,   -1,   -1,   0xC7, 0x43, 4,    -1,   -1,   -1,   -1});
    engine.expect(destroy + 0x72, {0xC7, 3, -1, -1, -1, -1, 0x5B, 0xC3});
    const auto* key_values_table = read<const std::byte*>(construct + 0x31);
    require(key_values_table == read<const std::byte*>(destroy + 0xA) &&
                read<const std::byte*>(construct + 0x38) == read<const std::byte*>(destroy + 0x11),
            "Shared ESP key-values constructor/destructor tables disagree");
    require_class(engine, key_values_table,
                  ".?AV?$CNetMessagePB@$0BC@VCCLCMsg_CmdKeyValues@@$0A@$00@@");
    (void)engine.call(construct + 0x13);
    // The complete feature constructor consumes and clears these CALL cells
    // before deriving its four genuine engine functions in native order.
    data.native_cell<const std::byte*>(0x4346D668) = message;
    data.native_cell<const std::byte*>(0x4346D66C) = receiver;
}
void initialize_build_shared_esp_bindings(NativeDataImage& data) {
    initialize_build_shared_esp_bindings(data, native_image_environment());
}
} // namespace skeet::engine
