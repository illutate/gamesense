#include "skeet/engine/build_window_bindings.hpp"
#include "skeet/engine/module_range_initialization.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <span>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
struct SectionProfile {
    char name[8];
    std::uint32_t rva, size, flags;
};
struct BuildProfile {
    std::uint32_t nt_offset, timestamp, image_size, checksum;
    std::uint16_t section_count;
    std::array<SectionProfile, 3> sections;
};
// Only the three sections used below are fingerprinted; every section's
// virtual extent is also checked against the declared image size.
constexpr BuildProfile engine_build{0x150,
                                    0x64EE7E86,
                                    0x39C0000,
                                    0x64174E,
                                    7,
                                    {{{".text", 0x1000, 0x451C32, 0x60000020},
                                      {".rdata", 0x453000, 0xD4766, 0x40000040},
                                      {".data", 0x528000, 0x3427638, 0xC0000040}}}};
constexpr BuildProfile studio_build{0x110,
                                    0x63DC118B,
                                    0x45E000,
                                    0x9DB8B,
                                    6,
                                    {{{".text", 0x1000, 0x717A2, 0x60000020},
                                      {".rdata", 0x73000, 0x1698E, 0x40000040},
                                      {".data", 0x8A000, 0x3C576C, 0xC0000040}}}};
struct Image {
    std::byte* base;
    const BuildProfile& build;
    bool contains(const void* pointer, std::size_t length, std::size_t section) const noexcept {
        const auto& region = build.sections[section];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= begin && length <= region.size && address - begin <= region.size - length;
    }
    std::uint32_t rva(const void* pointer) const noexcept {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) -
                                          reinterpret_cast<std::uintptr_t>(base));
    }
};
Image checked_image(void* module, const BuildProfile& profile) {
    require(module != nullptr, "Required engine/studiorender module is not loaded");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D, "Build binding requires a DOS image header");
    require(read<std::uint32_t>(base + 0x3C) == profile.nt_offset,
            "Unsupported engine/studiorender NT-header position");
    const auto* nt = base + profile.nt_offset;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == profile.section_count &&
                read<std::uint16_t>(nt + 0x14) == 0xE0 && read<std::uint16_t>(nt + 0x18) == 0x10B,
            "Unsupported engine/studiorender PE32 layout");
    require(read<std::uint32_t>(nt + 8) == profile.timestamp &&
                read<std::uint32_t>(nt + 0x50) == profile.image_size &&
                read<std::uint32_t>(nt + 0x58) == profile.checksum,
            "Unsupported engine/studiorender build identity");
    std::array<bool, 3> found{};
    for (std::uint16_t index = 0; index < profile.section_count; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= profile.image_size && size <= profile.image_size - rva,
                "Loaded PE section exceeds its image");
        for (std::size_t wanted = 0; wanted < profile.sections.size(); ++wanted) {
            const auto& expected = profile.sections[wanted];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[wanted] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Unsupported engine/studiorender section fingerprint");
            found[wanted] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Required engine/studiorender section is missing");
    return {base, profile};
}
bool matches(const std::byte* code, std::span<const int> pattern) noexcept {
    for (std::size_t index = 0; index < pattern.size(); ++index)
        if (pattern[index] >= 0 && static_cast<unsigned char>(code[index]) != pattern[index])
            return false;
    return true;
}
void expect_code(const Image& image, const std::byte* code, std::initializer_list<int> pattern) {
    require(image.contains(code, pattern.size(), 0),
            "Build binding code is outside executable text");
    require(matches(code, {pattern.begin(), pattern.size()}),
            "Loaded engine/studiorender instruction fingerprint differs");
}
const std::byte* unique_code(const Image& image, std::initializer_list<int> pattern,
                             std::uint32_t expected_rva) {
    const auto& text = image.build.sections[0];
    const std::byte* found = nullptr;
    for (std::uint32_t offset = 0; offset <= text.size - pattern.size(); ++offset) {
        const auto* code = image.base + text.rva + offset;
        if (!matches(code, {pattern.begin(), pattern.size()}))
            continue;
        require(found == nullptr, "Loaded engine/studiorender instruction anchor is ambiguous");
        found = code;
    }
    require(found && image.rva(found) == expected_rva,
            "Loaded engine/studiorender instruction anchor is unsupported");
    return found;
}
const std::byte* absolute(const Image& image, const std::byte* operand, std::size_t section,
                          std::size_t length) {
    const auto* pointer =
        reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(operand)));
    require(image.contains(pointer, length, section),
            "Decoded engine/studiorender pointer has invalid section provenance");
    return pointer;
}
const std::byte* relative_call(const Image& image, const std::byte* instruction,
                               std::uint32_t expected_rva) {
    expect_code(image, instruction, {0xE8, -1, -1, -1, -1});
    const auto offset =
        std::int64_t(image.rva(instruction)) + 5 + read<std::int32_t>(instruction + 1);
    require(offset >= 0 && offset < image.build.image_size,
            "Decoded engine/studiorender call leaves its image");
    const auto* target = image.base + static_cast<std::size_t>(offset);
    require(image.contains(target, 1, 0) && image.rva(target) == expected_rva,
            "Decoded engine/studiorender call target is unsupported");
    return target;
}
const std::byte* range_validator(const Image& image, std::uint32_t expected_rva) {
    return unique_code(image,
                       {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x33, 0xC0, 0x57, 0x8B, 0x7D, 0x08,
                        0x8B, 0x8E, 0x80, 0x02, 0,    0,    0x85, 0xC9, 0x7E, 0x16, 0x3B, 0x3C},
                       expected_rva);
}
void module_name_operand(const Image& image, const std::byte* operand, const char* name) {
    const auto length = std::strlen(name) + 1;
    const auto* string = absolute(image, operand, 1, length);
    require(std::memcmp(string, name, length) == 0,
            "Range initializer has an unexpected module-name operand");
}
struct Acquired {
    const std::byte* guard;
    void** interface_slot;
    std::uintptr_t rtti;
    const std::byte* engine_initialize;
    const std::byte* engine_get;
    const std::byte* studio_initialize;
    ModuleAddressRange** studio_holder;
};
Acquired acquire(const Image& engine, const Image& studio) {
    const auto* window =
        unique_code(engine, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xA1, -1,   -1,   -1,   -1,   0xA3,
                             -1,   -1,   -1,   -1,   0x8B, 0x0D, -1,   -1,   -1,   -1,   0x89, 0x0D,
                             -1,   -1,   -1,   -1,   0x8B, 0x15, -1,   -1,   -1,   -1,   0x8B, 0x02,
                             0x8B, 0x0D, -1,   -1,   -1,   -1,   0x8B, 0x50, 0x24, 0xFF, 0xD2},
                    0x22F060);
    const auto* guard = window + 0x2F;
    const auto* slot = absolute(engine, window + 0x1E, 2, 4);
    require(read<std::uint32_t>(window + 0x26) == reinterpret_cast<std::uintptr_t>(slot) &&
                engine.rva(slot) == 0x52C810,
            "Game-window caller does not use one verified IGame global");
    const std::array<std::byte, 4> epilogue{std::byte{0x8B}, std::byte{0xE5}, std::byte{0x5D},
                                            std::byte{0xC3}};
    // CRC32 on four bytes is injective. This exact epilogue has the native
    // B94D4C7E checksum; locating its first occurrence mirrors the valid scan.
    auto* continuation = guard;
    while (engine.contains(continuation, 4, 0) && std::memcmp(continuation, epilogue.data(), 4))
        ++continuation;
    require(engine.contains(continuation, 4, 0) && engine.rva(continuation) == 0x22F15F,
            "Game-window caller has an unsupported CRC continuation");
    const auto* object = absolute(engine, slot, 2, 12);
    const auto* table = absolute(engine, object, 1, 25 * 4);
    require(engine.contains(table - 4, 4, 1), "IGame vtable lacks its RTTI prefix");
    const auto* locator = absolute(engine, table - 4, 1, 20);
    require(read<std::uint32_t>(locator) == 0 && read<std::uint32_t>(locator + 4) == 0 &&
                read<std::uint32_t>(locator + 8) == 0,
            "IGame RTTI locator is not the observed CGame layout");
    const auto* type = absolute(engine, locator + 12, 2, 20);
    absolute(engine, locator + 16, 1, 16);
    require(std::memcmp(type + 8, ".?AVCGame@@", 12) == 0, "IGame RTTI type is not CGame");
    const auto* get_window = absolute(engine, table + 9 * 4, 0, 4);
    expect_code(engine, get_window, {0x8B, 0x41, 0x08, 0xC3});

    const auto* engine_validator = range_validator(engine, 0x2BD710);
    const auto* engine_initialize = relative_call(engine, engine_validator + 0x61, 0x2BDC20);
    expect_code(engine, engine_initialize,
                {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x08, 0x01, 0, 0, 0x56, 0x57, 0x8B, 0x3D, -1, -1, -1,
                 -1, 0x33, 0xF6});
    absolute(engine, engine_initialize + 0xD, 1, 4);
    module_name_operand(engine, engine_initialize + 0x19, "client.dll");
    module_name_operand(engine, engine_initialize + 0x23, "engine.dll");
    const auto* engine_append = relative_call(engine, engine_initialize + 0x25F, 0x2BDFF0);
    expect_code(engine, engine_append, {0x56, 0x8B, 0x71, 0x3C, 0xB8, 0x0B, 0x01, 0,    0,    0x03,
                                        0xF1, 0x66, 0x39, 0x46, 0x18, 0x75, 0x4A, 0x53, 0x8B, 0x5E,
                                        0x50, 0x57, 0x8B, 0x7E, 0x2C, 0x03, 0xF9, 0x03, 0xDF});
    const auto* engine_get = relative_call(engine, engine_append + 0x1D, 0x2BD020);
    expect_code(engine, engine_get,
                {0xA1, -1,   -1,   -1,   -1,   0x85, 0xC0, 0x75, 0x45, 0xA1, -1,   -1,   -1,   -1,
                 0x68, 0x0C, 0x03, 0,    0,    0x8B, 0x08, 0x8B, 0x01, 0xFF, 0x50, 0x04, 0x85, 0xC0,
                 0x74, 0x24, 0xC7, 0x80, 0x80, 0x02, 0,    0,    0,    0,    0,    0,    0xC7, 0x80,
                 0x04, 0x03, 0,    0,    0,    0,    0,    0,    0xC7, 0x80, 0x08, 0x03, 0,    0,
                 0,    0,    0,    0,    0xA3, -1,   -1,   -1,   -1,   0xC3, 0xC7, 0x05, -1,   -1,
                 -1,   -1,   0,    0,    0,    0,    0x33, 0xC0, 0xC3});
    const auto* engine_holder = absolute(engine, engine_get + 1, 2, 4);
    require(read<std::uint32_t>(engine_get + 0x3D) ==
                    reinterpret_cast<std::uintptr_t>(engine_holder) &&
                read<std::uint32_t>(engine_get + 0x44) ==
                    reinterpret_cast<std::uintptr_t>(engine_holder),
            "Engine range getter does not publish its own range holder");
    absolute(engine, engine_get + 0xA, 1, 4);

    const auto* studio_validator = range_validator(studio, 0x57830);
    const auto* studio_initialize = relative_call(studio, studio_validator + 0x61, 0x57DF0);
    expect_code(studio, studio_initialize,
                {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x08, 0x01, 0,  0,    0x53,
                 0x56, 0x57, 0x8B, 0x3D, -1,   -1,   -1,   -1, 0x33, 0xF6});
    absolute(studio, studio_initialize + 0xE, 1, 4);
    module_name_operand(studio, studio_initialize + 0x1A, "client.dll");
    module_name_operand(studio, studio_initialize + 0x29, "engine.dll");
    const auto* studio_append = relative_call(studio, studio_initialize + 0x27C, 0x570B0);
    expect_code(studio, studio_append,
                {0x53, 0x56, 0x8B, 0x35, -1,   -1,   -1,   -1,   0x8B, 0xDA, 0x57, 0x8B, 0xF9, 0x85,
                 0xF6, 0x75, 0x3F, 0xA1, -1,   -1,   -1,   -1,   0x68, 0x0C, 0x03, 0,    0,    0x8B,
                 0x08, 0x8B, 0x01, 0xFF, 0x50, 0x04, 0x8B, 0xF0, 0x85, 0xF6, 0x74, 0x20, 0xC7, 0x86,
                 0x80, 0x02, 0,    0,    0,    0,    0,    0,    0xC7, 0x86, 0x04, 0x03, 0,    0,
                 0,    0,    0,    0,    0xC7, 0x86, 0x08, 0x03, 0,    0,    0,    0,    0,    0,
                 0xEB, 0x02, 0x33, 0xF6, 0x89, 0x35, -1,   -1,   -1,   -1});
    const auto* studio_holder = absolute(studio, studio_append + 4, 2, 4);
    require(studio.rva(studio_holder) == 0x44E2EC &&
                read<std::uint32_t>(studio_append + 0x4C) ==
                    reinterpret_cast<std::uintptr_t>(studio_holder),
            "Studio range appender does not publish its verified range holder");
    absolute(studio, studio_append + 0x12, 1, 4);
    return {guard,
            reinterpret_cast<void**>(const_cast<std::byte*>(slot)),
            reinterpret_cast<std::uintptr_t>(locator),
            engine_initialize,
            engine_get,
            studio_initialize,
            reinterpret_cast<ModuleAddressRange**>(const_cast<std::byte*>(studio_holder))};
}
}

void initialize_build_window_bindings(NativeDataImage& data,
                                      const LoadedModuleEnvironment& modules) {
    require(sizeof(void*) == 4, "Loaded game-window bindings require the original x86 ABI");
    require(data.size() >= NativeDataImage::image_size,
            "Loaded game-window bindings require the complete canonical data image");
    require(modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
            "Loaded game-window bindings require the actual module list and lock imports");
    const auto engine = checked_image(
        find_loaded_module(modules, loaded_module_name_hash(u"engine.dll", -1), true, true),
        engine_build);
    const auto studio = checked_image(
        find_loaded_module(modules, loaded_module_name_hash(u"studiorender.dll", -1), true, true),
        studio_build);
    const auto bindings = acquire(engine, studio);
    data.native_cell<const std::byte*>(0x43467288u) = bindings.guard;
    data.native_cell<void**>(0x43468B2Cu) = bindings.interface_slot;
    data.native_cell<std::uintptr_t>(0x43469288u) = bindings.rtti;
    // These are literal inactive rows in both pinned payload and working dump.
    // Their companion cells are never read; no captured value is installed.
    data.native_cell<std::uint32_t>(0x434675D8u) = 1;
    data.native_cell<std::uint32_t>(0x4346A58Cu) = 1;
    data.native_cell<ModuleRangeInitialize>(0x4346837Cu) =
        reinterpret_cast<ModuleRangeInitialize>(const_cast<std::byte*>(bindings.engine_initialize));
    data.native_cell<ModuleRangeGet>(0x43467570u) =
        reinterpret_cast<ModuleRangeGet>(const_cast<std::byte*>(bindings.engine_get));
    data.native_cell<ModuleRangeInitialize>(0x43468234u) =
        reinterpret_cast<ModuleRangeInitialize>(const_cast<std::byte*>(bindings.studio_initialize));
    data.native_cell<ModuleAddressRange**>(0x43468C98u) = bindings.studio_holder;
}
} // namespace skeet::engine
