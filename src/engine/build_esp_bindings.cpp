#include "skeet/engine/build_esp_bindings.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>
#include <span>
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
constexpr Region text{0x1000, 0xB38409, 0x60000020}, rdata{0xB3A000, 0x26629C, 0x40000040},
    objects{0xDA1000, 0x460C3E0, 0xC0000040};
struct Client {
    std::byte* base;
    bool contains(const void* pointer, std::size_t length, Region region) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer),
                   first = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= first && length <= region.size && address - first <= region.size - length;
    }
    void expect(const std::byte* code, std::initializer_list<int> pattern) const {
        require(contains(code, pattern.size(), text), "ESP method is outside genuine client text");
        std::size_t index = 0;
        for (const auto value : pattern) {
            require(value < 0 || static_cast<unsigned char>(code[index]) == value,
                    "ESP client instruction fingerprint differs");
            ++index;
        }
    }
    const std::byte* unique(std::initializer_list<int> pattern) const {
        const std::byte* result = nullptr;
        for (std::uint32_t offset = 0; offset <= text.size - pattern.size(); ++offset) {
            const auto* code = base + text.rva + offset;
            std::size_t index = 0;
            bool match = true;
            for (const auto value : pattern) {
                if (value >= 0 && static_cast<unsigned char>(code[index]) != value) {
                    match = false;
                    break;
                }
                ++index;
            }
            if (!match)
                continue;
            require(!result, "ESP client function anchor is ambiguous");
            result = code;
        }
        require(result != nullptr, "ESP client function anchor is missing");
        return result;
    }
    const std::byte* call(const std::byte* instruction) const {
        expect(instruction, {0xE8, -1, -1, -1, -1});
        const auto* result = instruction + 5 + read<std::int32_t>(instruction + 1);
        require(contains(result, 1, text), "ESP helper call leaves genuine client text");
        return result;
    }
};
Client checked(NativeDataImage& data, NativeImageEnvironment images) {
    auto* module = data.native_cell<std::byte*>(0x4346A55C);
    require(module && images.image_nt_header,
            "ESP entries require the loaded client and PE-header service");
    const auto* nt = images.image_nt_header(module);
    require(nt == module + 0x150 && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x150 && read<std::uint32_t>(nt) == 0x4550 &&
                read<std::uint16_t>(nt + 4) == 0x14C && read<std::uint16_t>(nt + 6) == 7 &&
                read<std::uint16_t>(nt + 20) == 0xE0 && read<std::uint16_t>(nt + 24) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 80) == 0x5509000 &&
                read<std::uint32_t>(nt + 88) == 0xFA635F,
            "Unsupported ESP client PE32 build");
    constexpr std::array<Region, 3> regions{text, rdata, objects};
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        require(read<std::uint32_t>(section + 12) == regions[index].rva &&
                    read<std::uint32_t>(section + 8) == regions[index].size &&
                    read<std::uint32_t>(section + 36) == regions[index].flags,
                "ESP client section geometry differs");
    }
    return {module};
}
void pattern_adapter(void* context, std::uint32_t weapon, std::int32_t mode, std::int32_t pellet,
                     float& angle, float& radius) {
    const auto entry = *static_cast<const ShotgunPatternEntry*>(context);
    entry(weapon, mode, pellet, &angle, &radius);
}
}
effects::GlowEngine BuildEspBindings::glow_engine() const {
    if (!data.native_cell<void*>(0x4346832C))
        throw std::logic_error("The genuine glow registration entry is not initialized");
    return {data.native_cell<effects::GlowRegistry*>(0x43467460),
            data.native_cell<void*>(0x4346832C)};
}
rage::ShotgunPattern BuildEspBindings::shotgun_pattern() const {
    auto& entry = data.native_cell<ShotgunPatternEntry>(0x43467DDC);
    if (!entry)
        throw std::logic_error("The genuine shotgun pattern entry is not initialized");
    return {&entry, pattern_adapter};
}
BuildEspBindings initialize_build_esp_bindings(NativeDataImage& data,
                                               NativeImageEnvironment images) {
    const auto client = checked(data, images);
    const auto* glow =
        client.unique({0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xF9, 0xF3, 0x0F, 0x11,
                       0x5D, 0xFC, 0x8B, 0x5F, 0x14, 0x83, 0xFB, 0xFF, 0x75, 0x0C, 0xFF, 0x77,
                       0x0C, 0xE8, -1,   -1,   -1,   -1,   0x8B, 0xD8, 0xEB, 0x11});
    client.expect(client.call(glow + 0x19),
                  {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0x4E, 0x0C,
                   0x8B, 0x56, 4,    0x8D, 0x41, 1,    0x3B, 0xC2, 0x7E, 0x0B});
    client.expect(glow + 0xD8, {0xE8, -1, -1, -1, -1, 0x5F, 0x5E, 0x8B, 0xC3, 0x5B, 0x8B, 0xE5,
                                0x5D, 0xC2, 0x14, 0});
    client.expect(client.call(glow + 0xD8),
                  {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x6A, 5, 0x6A, 0x34});
    const auto* pattern =
        client.unique({0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x56, 0x8B, 0x75, 8,    0x8D, 0x45,
                       0xF4, 0x50, 0xB9, -1,   -1,   -1,   -1,   0x66, 0x89, 0x75, 0xF4, 0xE8,
                       -1,   -1,   -1,   -1,   0x0F, 0xB7, 0xC0, 0xB9, 0xFF, 0xFF, 0,    0,
                       0x66, 0x3B, 0xC1, 0x75, 9,    0x56, 0xE8, -1,   -1,   -1,   -1});
    const auto* cache = read<const std::byte*>(pattern + 0xF);
    require(client.contains(cache, 0x18, objects) &&
                read<const std::byte*>(pattern + 0x36) == cache + 4,
            "Shotgun pattern lookup uses inconsistent genuine cache operands");
    client.expect(client.call(pattern + 0x17),
                  {0x55, 0x8B, 0xEC, 0x56, 0x57, 0x8B, 0xF9, 0xB8, 0xFF, 0xFF, 0,
                   0,    0x0F, 0xB7, 0x77, 0x10, 0x66, 0x3B, 0xF0, 0x74, 0x57});
    const auto* construct = client.call(pattern + 0x2A);
    client.expect(construct,
                  {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x66, 0x8B, 0x75, 8,    0x8D, 0x45,
                   0xF4, 0x57, 0x50, 0xB9, -1,   -1,   -1,   -1,   0x66, 0x89, 0x75, 0xF4});
    require(read<const std::byte*>(construct + 0x11) == cache,
            "Shotgun pattern cache and creator operands disagree");
    client.expect(pattern + 0x49,
                  {0x8B, 0x55, 0x10, 0x83, 0xFA, 0x40, 0x73, 0x1F, 0x8B, 0x45, 0x14, 0x8B, 0x8C,
                   0xD6, 4,    4,    0,    0,    0x89, 8,    0x8B, 0x45, 0x18, 0x8B, 0x8C, 0xD6,
                   8,    4,    0,    0,    0x89, 8,    0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x14, 0});
    client.expect(pattern + 0xAE, {0xC2, 0x14, 0});
    client.expect(pattern + 0x82, {0xFF, 0x15, -1, -1, -1, -1});
    client.expect(pattern + 0x9C, {0xFF, 0x15, -1, -1, -1, -1});
    const auto* random_slot = read<const std::byte*>(pattern + 0x84);
    require(client.contains(random_slot, 4, rdata) &&
                read<const std::byte*>(pattern + 0x9E) == random_slot,
            "Shotgun pattern RandomFloat imports disagree");
    auto* vstdlib = data.native_cell<std::byte*>(0x43468A7C);
    require(vstdlib != nullptr, "Shotgun pattern requires the loaded vstdlib module");
    auto* random = resolve_pe_export(vstdlib, netvar_name_hash("RandomFloat"), 0, true, images);
    require(random && read<const std::byte*>(random_slot) == random,
            "Shotgun pattern does not use genuine vstdlib RandomFloat");
    data.native_cell<void*>(0x4346832C) = const_cast<std::byte*>(glow);
    data.native_cell<ShotgunPatternEntry>(0x43467DDC) =
        reinterpret_cast<ShotgunPatternEntry>(const_cast<std::byte*>(pattern));
    return {data};
}
BuildEspBindings initialize_build_esp_bindings(NativeDataImage& data) {
    return initialize_build_esp_bindings(data, native_image_environment());
}
} // namespace skeet::engine
