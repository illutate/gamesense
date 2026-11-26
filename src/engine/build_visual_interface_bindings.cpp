#include "skeet/engine/build_visual_interface_bindings.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/rtti.hpp"
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
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Section {
    std::uint32_t rva, size, flags;
};
constexpr Section text{0x1000, 0xB38409, 0x60000020};
constexpr Section rdata{0xB3A000, 0x26629C, 0x40000040};
constexpr Section objects{0xDA1000, 0x460C3E0, 0xC0000040};
struct Client {
    std::byte* base;
    bool contains(const void* pointer, std::size_t size, Section section) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto first = reinterpret_cast<std::uintptr_t>(base) + section.rva;
        return address >= first && size <= section.size && address - first <= section.size - size;
    }
    void expect(const std::byte* code, std::span<const int> bytes) const {
        require(contains(code, bytes.size(), text), "Visual method is outside genuine client text");
        for (std::size_t index = 0; index < bytes.size(); ++index)
            require(bytes[index] < 0 || static_cast<unsigned char>(code[index]) == bytes[index],
                    "Visual method does not match the supported client build");
    }
    void expect(const std::byte* code, std::initializer_list<int> bytes) const {
        expect(code, {bytes.begin(), bytes.size()});
    }
    const std::byte* unique(std::span<const int> pattern) const {
        const std::byte* result = nullptr;
        for (std::uint32_t offset = 0; offset <= text.size - pattern.size(); ++offset) {
            const auto* code = base + text.rva + offset;
            bool match = true;
            for (std::size_t index = 0; index < pattern.size(); ++index)
                if (pattern[index] >= 0 &&
                    static_cast<unsigned char>(code[index]) != pattern[index]) {
                    match = false;
                    break;
                }
            if (!match)
                continue;
            require(!result, "Visual client instruction anchor is ambiguous");
            result = code;
        }
        require(result != nullptr, "Visual client instruction anchor is missing");
        return result;
    }
};
Client checked_client(NativeDataImage& data, NativeImageEnvironment images) {
    auto* module = data.native_cell<std::byte*>(0x4346A55C);
    require(module && images.image_nt_header,
            "Visual objects require the actual client module and PE-header service");
    const auto* nt = images.image_nt_header(module);
    require(nt == module + 0x150 && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x150 && read<std::uint32_t>(nt) == 0x4550 &&
                read<std::uint16_t>(nt + 4) == 0x14C && read<std::uint16_t>(nt + 6) == 7 &&
                read<std::uint16_t>(nt + 20) == 0xE0 && read<std::uint16_t>(nt + 24) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 80) == 0x5509000 &&
                read<std::uint32_t>(nt + 88) == 0xFA635F,
            "Unsupported visual-object client PE32 build");
    constexpr std::array<Section, 3> expected{text, rdata, objects};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        require(read<std::uint32_t>(section + 12) == expected[index].rva &&
                    read<std::uint32_t>(section + 8) == expected[index].size &&
                    read<std::uint32_t>(section + 36) == expected[index].flags,
                "Visual client section geometry differs from the supported build");
    }
    return {module};
}
void** table(const Client& image, const char* name, std::size_t slots,
             NativeImageEnvironment images, std::uint32_t object_offset = 0) {
    auto* type = find_rtti_type(image.base, netvar_name_hash(name), images);
    require(type && image.contains(type, 8 + std::strlen(name) + 1, objects) &&
                !std::strcmp(type->name, name),
            "Required genuine visual client RTTI type is missing");
    auto** result = find_rtti_vtable_at_offset(image.base, type, object_offset, images);
    require(result && image.contains(result, slots * 4, rdata) &&
                image.contains(result - 1, 4, rdata),
            "Required visual client vtable is missing or outside read-only data");
    auto* locator = read<const std::byte*>(reinterpret_cast<const std::byte*>(result - 1));
    require(image.contains(locator, 20, rdata) && read<std::uint32_t>(locator) == 0 &&
                read<std::uint32_t>(locator + 4) == object_offset &&
                read<std::uint32_t>(locator + 8) == 0 &&
                read<RttiTypeDescriptor*>(locator + 12) == type,
            "Visual client RTTI locator is inconsistent");
    for (std::size_t slot = 0; slot < slots; ++slot)
        require(image.contains(result[slot], 1, text),
                "Visual vtable method is outside genuine client text");
    return result;
}
std::byte* unique_instance(const Client& image, void** vtable, std::size_t extent) {
    std::byte* result = nullptr;
    for (std::uint32_t offset = 0; offset <= objects.size - extent; offset += 4) {
        auto* object = image.base + objects.rva + offset;
        if (read<void**>(object) != vtable)
            continue;
        require(!result, "Visual client inline instance is ambiguous");
        result = object;
    }
    require(result != nullptr, "Required visual client inline instance has not been constructed");
    return result;
}
}
BuildVisualInterfaceBindings
initialize_build_visual_interface_bindings(NativeDataImage& data, NativeImageEnvironment images) {
    const auto image = checked_client(data, images);
    auto** view_table = table(image, ".?AVCCSViewRender@@", 13, images);
    auto* view = unique_instance(image, view_table, 0x5A8);
    // Real static initialization writes the same CCSViewRender table and its
    // three following fields. The object is derived from the store operand.
    std::array<int, 17> view_anchor{0xC7, 0x05, -1,   -1, -1, -1, -1, -1, -1,
                                    -1,   0xC6, 0x05, -1, -1, -1, -1, 0};
    const auto table_value = reinterpret_cast<std::uintptr_t>(view_table);
    for (unsigned index = 0; index < 4; ++index)
        view_anchor[6 + index] = int((table_value >> (index * 8)) & 255u);
    const auto* initialization = image.unique(view_anchor);
    require(read<std::byte*>(initialization + 2) == view &&
                read<std::byte*>(initialization + 12) == view + 0x5A0,
            "CCSViewRender initialization operands disagree with the genuine inline instance");
    const auto* get_view = static_cast<const std::byte*>(view_table[12]);
    image.expect(get_view, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 8, 0x83, 0xF8, 0xFF, 0x75, 7,
                            0x8D, 0x41, 4,    0x5D, 0xC2, 4, 0,    0x69, 0xC0, 0x4C, 1,
                            0,    0,    0x83, 0xC0, 4,    3, 0xC1, 0x5D, 0xC2, 4,    0});

    auto** temp_table = table(image, ".?AVC_TempEntsSystem@@", 10, images);
    auto* temp = unique_instance(image, temp_table, 0x14);
    // The same inline object is both ECX and the absolute vtable read in this
    // genuine player-effect call. This also distinguishes its static instance
    // from an unrelated object that happens to share a class table.
    static constexpr int temp_anchor[]{0xB9, -1,   -1,   -1,   -1,   0x50, 0xA1, -1,   -1,   -1,
                                       -1,   0xFF, 0x50, 0x7C, 0x8D, 0x4D, 0xE4, 0xC7, 0x45, 0xDC};
    const auto* temp_use = image.unique(temp_anchor);
    require(read<std::byte*>(temp_use + 1) == temp && read<std::byte*>(temp_use + 7) == temp,
            "Temporary-entity code operands disagree with the genuine inline instance");
    const auto* ring = static_cast<const std::byte*>(temp_table[9]);
    image.expect(ring,
                 {0x55, 0x8B, 0xEC, 0x8B, 1,    0xFF, 0x75, 8,    0x8B, 0x40, 4,    0xFF, 0xD0,
                  0x84, 0xC0, 0x75, 0x5F, 0xFF, 0x75, 0x50, 0xF3, 0x0F, 0x10, 0x45, 0x38});
    image.expect(ring + 0x70, {0x5D, 0xC2, 0x4C, 0}); //19 original stack arguments

    auto** beams_table = table(image, ".?AVCViewRenderBeams@@", 13, images);
    auto* beams = unique_instance(image, beams_table, 0x20);
    std::array<int, 20> beams_anchor{0xC7, 5,  -1, -1, -1, -1,   -1, -1, -1, -1,
                                     0xB9, -1, -1, -1, -1, 0xE9, -1, -1, -1, -1};
    const auto beams_value = reinterpret_cast<std::uintptr_t>(beams_table);
    for (unsigned index = 0; index < 4; ++index)
        beams_anchor[6 + index] = int((beams_value >> (index * 8)) & 255u);
    const auto* beams_init = image.unique(beams_anchor);
    require(read<std::byte*>(beams_init + 2) == beams && read<std::byte*>(beams_init + 11) == beams,
            "Beam-renderer constructor operands disagree with the genuine inline instance");
    image.expect(static_cast<const std::byte*>(beams_table[12]),
                 {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 8,    0x0F, 0x57, 0xC9,
                  0x57, 0x8B, 0xF9, 0xF3, 0x0F, 0x10, 0x46, 0x40, 0x0F, 0x2E,
                  0xC1, 0x9F, 0xF6, 0xC4, 0x44, 0x7B, 0x17, 0x6A, 1});

    //43467B54 is this class's IWeaponSystem subobject at complete-object+C,
    // not its primary game-system endpoint. Its real RTTI COL has offsetC.
    auto** weapon_table = table(image, ".?AVCCSWeaponSystem@@", 3, images, 0xC);
    auto* weapon_system = unique_instance(image, weapon_table, 0x30);
    std::array<int, 20> weapon_anchor{0xC7, 5, -1, -1, -1, -1, -1, -1, -1, -1,
                                      0xC7, 5, -1, -1, -1, -1, 0,  0,  0,  0};
    const auto weapon_value = reinterpret_cast<std::uintptr_t>(weapon_table);
    for (unsigned index = 0; index < 4; ++index)
        weapon_anchor[6 + index] = int((weapon_value >> (index * 8)) & 255u);
    const auto* weapon_init = image.unique(weapon_anchor);
    require(read<std::byte*>(weapon_init + 2) == weapon_system &&
                read<std::byte*>(weapon_init + 12) == weapon_system + 8,
            "Weapon-system constructor operands disagree with the genuine inline instance");
    image.expect(static_cast<const std::byte*>(weapon_table[2]),
                 {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x8B, 0x5D, 8,
                  0x89, 0x4D, 0xF4, 0x81, 0xFB, 0xFF, 0xFF, 0,    0,    0x72,
                  9,    0x33, 0xC0, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 4,    0});
    // Source acquisition publishes this pair after validation; it does not
    // claim a separate recovered ordering for the loader's extra() writes.
    data.native_cell<void*>(0x4346A624) = view;
    data.native_cell<void*>(0x434679EC) = temp;
    data.native_cell<void*>(0x4346A898) = beams;
    data.native_cell<void*>(0x43467B54) = weapon_system;
    return {view, temp, beams, weapon_system};
}
BuildVisualInterfaceBindings initialize_build_visual_interface_bindings(NativeDataImage& data) {
    return initialize_build_visual_interface_bindings(data, native_image_environment());
}
} // namespace skeet::engine
