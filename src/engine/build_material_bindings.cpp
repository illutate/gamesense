#include "skeet/engine/build_material_bindings.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const void* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
}
struct Region {
    std::uint32_t rva, size, flags;
};
struct Build {
    std::uint32_t nt, sections, timestamp, size, checksum;
    std::array<Region, 3> regions;
};
constexpr Build material_build{0x118u,
                               6u,
                               0x63DC3DE4u,
                               0x114000u,
                               0xDE784u,
                               {{{0x1000u, 0x91D46u, 0x60000020u},
                                 {0x93000u, 0x28074u, 0x40000040u},
                                 {0xBC000u, 0x42480u, 0xC0000040u}}}};
constexpr Build localize_build{0x110u,
                               5u,
                               0x63DC12AFu,
                               0x3E000u,
                               0x38859u,
                               {{{0x1000u, 0x1FE99u, 0x60000020u},
                                 {0x21000u, 0xC3CCu, 0x40000040u},
                                 {0x2E000u, 0xC144u, 0xC0000040u}}}};
struct Image {
    std::byte* base;
    const Build& build;
    bool within(const void* pointer, std::size_t length, std::size_t region) const noexcept {
        const auto& bounds = build.regions[region];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto first = reinterpret_cast<std::uintptr_t>(base) + bounds.rva;
        return address >= first && length <= bounds.size && address - first <= bounds.size - length;
    }
};
Image checked(std::byte* module, const Build& build, NativeImageEnvironment environment) {
    require(module && read<std::uint16_t>(module) == 0x5A4Du &&
                read<std::uint32_t>(module + 0x3C) == build.nt,
            "Material bindings require the supported loaded module headers");
    require(environment.image_nt_header != nullptr,
            "Material bindings require the actual NT-header boundary");
    const auto* nt = environment.image_nt_header(module);
    require(nt == module + build.nt && read<std::uint32_t>(nt) == 0x4550u &&
                read<std::uint16_t>(nt + 4) == 0x14Cu &&
                read<std::uint16_t>(nt + 6) == build.sections &&
                read<std::uint16_t>(nt + 20) == 0xE0u && read<std::uint16_t>(nt + 24) == 0x10Bu &&
                read<std::uint32_t>(nt + 8) == build.timestamp &&
                read<std::uint32_t>(nt + 0x50) == build.size &&
                read<std::uint32_t>(nt + 0x58) == build.checksum,
            "Material module PE32 build identity differs");
    constexpr std::array<const char*, 3> names{{".text", ".rdata", ".data"}};
    std::array<bool, 3> found{};
    for (std::uint32_t index = 0; index < build.sections; ++index) {
        const auto* section = nt + 0xF8u + index * 40u;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= build.size && size <= build.size - rva,
                "Material module section exceeds its image");
        for (std::size_t wanted = 0; wanted < names.size(); ++wanted) {
            if (std::strncmp(reinterpret_cast<const char*>(section), names[wanted], 8))
                continue;
            const auto& expected = build.regions[wanted];
            require(!found[wanted] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Material module section fingerprint differs");
            found[wanted] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Material module sections are unavailable");
    return {module, build};
}
constexpr std::array<std::int16_t, 64> variable_pattern{{
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, -1,   -1,   -1,   -1,   0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xFA, 0x8B, 0xD9,
    0xE8, 0xDB, 0xFD, 0xFF, 0xFF, 0x8B, 0xF0, 0x89, 0x75, 0xF0, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0x85, 0xF6, 0x0F, 0x84, 0x80, 0x00, 0x00, 0x00, 0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x8B, 0xCF,
}};
effects::CreateMaterialVariable material_variable(const Image& image) {
    const std::byte* found = nullptr;
    const auto& text = image.build.regions[0];
    for (std::uint32_t offset = 0; offset <= text.size - variable_pattern.size(); ++offset) {
        const auto* candidate = image.base + text.rva + offset;
        bool matches = true;
        for (std::size_t i = 0; i < variable_pattern.size(); ++i)
            if (variable_pattern[i] >= 0 &&
                static_cast<unsigned char>(candidate[i]) != variable_pattern[i]) {
                matches = false;
                break;
            }
        if (!matches)
            continue;
        require(!found, "Material-variable instruction signature is ambiguous");
        found = candidate;
    }
    require(found == image.base + 0x24B10u,
            "Genuine supported material-variable function is unavailable");
    // Complete fastcall body: ECX is the material stored at object+20, EDX
    // supplies the interned variable name, and both exits use plain RET.
    require(image.within(read<const void*>(found + 6), 1, 0),
            "Material-variable exception handler has invalid provenance");
    return reinterpret_cast<effects::CreateMaterialVariable>(const_cast<std::byte*>(found));
}
void* localization(const Image& image, NativeImageEnvironment environment) {
    auto* registry = find_interface_registry(image.base, environment);
    require(registry && image.within(registry, sizeof(InterfaceRegistration), 2),
            "Localize_001 registry is unavailable in the loaded module");
    auto* registration = find_interface_registration(registry, netvar_name_hash("Localize_001"));
    require(registration && image.within(registration, sizeof(InterfaceRegistration), 2) &&
                image.within(registration->name, sizeof("Localize_001"), 1) &&
                std::strcmp(registration->name, "Localize_001") == 0,
            "Required exact Localize_001 registration is unavailable");
    const auto* creator = reinterpret_cast<const std::byte*>(registration->create);
    require(creator == image.base + 0x1AF0u && image.within(creator, 6, 0) &&
                creator[0] == std::byte{0xB8} && creator[5] == std::byte{0xC3},
            "Localize_001 creator differs from its proven singleton getter");
    auto* object = read<void*>(creator + 1);
    require(object == image.base + 0x38A70u && image.within(object, 4, 2),
            "Localization singleton has invalid module provenance");
    auto* table = read<void**>(object);
    require(image.within(table, 12 * 4, 1),
            "Localization virtual table is outside its real module");
    for (std::size_t slot = 0; slot < 12; ++slot)
        require(image.within(table[slot], 1, 0),
                "Localization method is outside its real executable section");
    return object;
}
}
BuildMaterialBindings initialize_build_material_bindings(NativeDataImage& data,
                                                         const LoadedModuleEnvironment& modules,
                                                         NativeImageEnvironment images) {
    require(modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
            "Material acquisition requires the real loaded-module environment");
    auto* module = static_cast<std::byte*>(find_loaded_module(
        modules, loaded_module_name_hash(u"materialsystem.dll", 18), true, true));
    require(module && module == data.native_cell<std::byte*>(0x4346A600u),
            "Material acquisition must use the canonically published loaded materialsystem module");
    auto* localize_module = static_cast<std::byte*>(
        find_loaded_module(modules, loaded_module_name_hash(u"localize.dll", 12), true, true));
    const auto material = checked(module, material_build, images);
    const auto localize = checked(localize_module, localize_build, images);
    const auto create = material_variable(material);
    auto* strings = localization(localize, images);
    data.native_cell<effects::CreateMaterialVariable>(0x43467A68u) = create;
    data.native_cell<void*>(0x4346A31Cu) = strings;
    return {data, create, strings};
}
BuildMaterialBindings initialize_build_material_bindings(NativeDataImage& data) {
    return initialize_build_material_bindings(data, native_loaded_module_environment(),
                                              native_image_environment());
}
} //namespace skeet::engine
