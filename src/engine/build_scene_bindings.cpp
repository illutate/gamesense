#include "skeet/engine/build_scene_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/rage/engine_ballistics.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace skeet::engine {
namespace {
constexpr std::array<BuildSceneClassRecipe, 20> classes{
    {{0x390u, 0x009E3601u, 0x4346A440u, "CBreakableSurface"},
     {0x398u, 0x0F0EDE49u, 0x43467478u, "CBaseEntity"},
     {0x3A0u, 0x8CBFEC01u, 0x43467E68u, "CCascadeLight"},
     {0x3A8u, 0xF89B2C53u, 0x43468CA0u, "CDynamicProp"},
     {0x3B0u, 0x52563AF3u, 0x43468E30u, "CBaseCSGrenadeProjectile"},
     {0x3B8u, 0x144AF01Cu, 0x43468DF8u, "CBaseCSGrenade"},
     {0x3C0u, 0xFD66DD64u, 0x434682ACu, "CWeaponCSBase"},
     {0x3C8u, 0x30F3B77Eu, 0x43467E88u, "CMolotovProjectile"},
     {0x3D0u, 0x392B5938u, 0x434674E8u, "CInferno"},
     {0x3D8u, 0x1DA776D4u, 0x4346A348u, "CParticleSystem"},
     {0x3E0u, 0xAF29E891u, 0x43467008u, "CPredictedViewModel"},
     {0x3E8u, 0xAEEB25F3u, 0x4346A7D8u, "CBaseWeaponWorldModel"},
     {0x3F0u, 0xB79BF5CAu, 0x43467C44u, "CKnife"},
     {0x3F8u, 0xD5A32634u, 0x43467DBCu, "CBaseAnimating"},
     {0x400u, 0xB66F52E5u, 0x434684C4u, "CSun"},
     {0x408u, 0xDBD65E05u, 0x4346841Cu, "CEconWearable"},
     {0x410u, 0xB4890975u, 0x4346A5A0u, "CFuncBrush"},
     {0x418u, 0x71D79C49u, 0x43468238u, "CSmokeGrenadeProjectile"},
     {0x420u, 0xD2B93652u, 0x434685FCu, "CBaseAnimatingOverlay"},
     {0x428u, 0x4A945FC9u, 0x4346719Cu, "CFuncLadder"}}};
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
constexpr std::array<Section, 3> sections{{{".text", 0x1000, 0xB38409, 0x60000020},
                                           {".rdata", 0xB3A000, 0x26629C, 0x40000040},
                                           {".data", 0xDA1000, 0x460C3E0, 0xC0000040}}};
struct Image {
    std::byte* base;
    bool within(const void* pointer, std::size_t size, std::size_t section) const noexcept {
        const auto& region = sections[section];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto first = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= first && size <= region.size && address - first <= region.size - size;
    }
};
Image checked_image(std::byte* module) {
    require(module && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x150,
            "Scene bindings require the supported loaded client image");
    const auto* nt = module + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Scene client PE32 build identity differs");
    std::array<bool, 3> found{};
    for (std::size_t i = 0; i < 7; ++i) {
        const auto* section = nt + 0xF8 + i * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva,
                "Scene client section exceeds its image");
        for (std::size_t j = 0; j < sections.size(); ++j) {
            const auto& expected = sections[j];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[j] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Scene client section fingerprint differs");
            found[j] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Required scene client section is missing");
    return {module};
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
            "Scene native instruction fingerprint differs");
}
const std::byte* unique(const Image& image, std::initializer_list<int> pattern, std::uint32_t rva) {
    const std::byte* found = nullptr;
    for (std::size_t i = 0; i <= sections[0].size - pattern.size(); ++i) {
        const auto* code = image.base + sections[0].rva + i;
        if (!matches(code, pattern))
            continue;
        require(!found, "Scene native code anchor is ambiguous");
        found = code;
    }
    require(found == image.base + rva, "Scene native code anchor is missing or unsupported");
    return found;
}
const std::byte* absolute(const Image& image, const std::byte* operand, std::size_t size,
                          std::size_t section) {
    const auto* pointer =
        reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(operand)));
    require(image.within(pointer, size, section),
            "Decoded scene pointer has invalid section provenance");
    return pointer;
}
void string(const Image& image, const std::byte* pointer, const char* expected,
            std::size_t section) {
    const auto size = std::strlen(expected) + 1;
    require(image.within(pointer, size, section) && std::memcmp(pointer, expected, size) == 0,
            "Scene native name operand differs");
}
void rtti_operand(const Image& image, const std::byte* operand, const char* expected) {
    const auto* type = absolute(image, operand, 8 + std::strlen(expected) + 1, 2);
    string(image, type + 8, expected, 2);
}
const std::byte* call_target(const Image& image, const std::byte* instruction, std::uint32_t rva) {
    expect(image, instruction, {0xE8, -1, -1, -1, -1});
    const auto offset =
        std::int64_t(instruction - image.base) + 5 + read<std::int32_t>(instruction + 1);
    require(offset == rva && offset >= 0 && offset < 0x5509000,
            "Scene native helper call target differs");
    const auto* target = image.base + static_cast<std::size_t>(offset);
    require(image.within(target, 1, 0), "Scene helper call leaves executable text");
    return target;
}
const std::byte* breakable_function(const Image& image) {
    const auto* function =
        unique(image, {0x55, 0x8B, 0xEC, 0x51, 0x56, 0x8B, 0xF1, 0x85, 0xF6, 0x74, 0x68, 0x83,
                       0xBE, 0,    0x01, 0,    0,    0,    0x7D, 0x10, 0x8B, 0x06, 0xFF, 0x90,
                       0xEC, 0x01, 0,    0,    0x85, 0xC0, 0x0F, 0x8F, 0xE2, 0,    0,    0,
                       0x80, 0xBE, 0x80, 0x02, 0,    0,    0x02, 0x75, 0x46},
               0x320BD0);
    expect(image, function + 0x4D, {0x6A, 0, 0x68, -1,   -1, -1, -1, 0x68, -1,   -1,   -1,  -1,
                                    0x6A, 0, 0x56, 0xE8, -1, -1, -1, -1,   0x83, 0xC4, 0x14});
    rtti_operand(image, function + 0x50, ".?AVIMultiplayerPhysics@@");
    rtti_operand(image, function + 0x55, ".?AVC_BaseEntity@@");
    call_target(image, function + 0x5C, 0xAC99C5);
    expect(image, function + 0x7A,
           {0xBA, -1, -1, -1, -1, 0x8B, 0xCE, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x75, 0x28});
    expect(image, function + 0x8A,
           {0xBA, -1, -1, -1, -1, 0x8B, 0xCE, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x75, 0x18});
    string(image, absolute(image, function + 0x7B, 15, 1), "func_breakable", 1);
    string(image, absolute(image, function + 0x8B, 20, 1), "func_breakable_surf", 1);
    call_target(image, function + 0x81, 0x1A7480);
    call_target(image, function + 0x91, 0x1A7480);
    expect(image, function + 0xD2, {0x6A, 0,  0x68, -1, -1,   -1,   -1, 0x68, -1, -1,
                                    -1,   -1, 0x6A, 0,  0x56, 0xE8, -1, -1,   -1, -1});
    rtti_operand(image, function + 0xD5, ".?AVIBreakableWithPropData@@");
    rtti_operand(image, function + 0xDA, ".?AVC_BaseEntity@@");
    call_target(image, function + 0xE1, 0xAC99C5);
    expect(image, function + 0x106, {0xB0, 0x01, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    return function;
}
void validate_class_list(const Image& image, void* base_client) {
    require(image.within(base_client, sizeof(void*), 2),
            "Scene bindings require the actual VClient018 receiver");
    const auto* table = read<const std::byte*>(base_client);
    // A source hook may own the table allocation. Its unchanged virtual8 must
    // still be the verified genuine client GetAllClasses getter.
    const auto* getter = read<const std::byte*>(table + 8 * sizeof(void*));
    expect(image, getter, {0xA1, -1, -1, -1, -1, 0xC3});
    require(getter == image.base + 0x260A50, "Scene GetAllClasses entry differs");
    auto* node = read<ClientClass*>(absolute(image, getter + 1, 4, 2));
    for (std::size_t i = 0; i < 289; ++i) {
        require(image.within(node, sizeof(ClientClass), 2),
                "Scene class list is incomplete or has invalid provenance");
        require((node->create || node->create_event) &&
                    (!node->create || image.within(node->create, 1, 0)) &&
                    (!node->create_event || image.within(node->create_event, 1, 0)) &&
                    image.within(node->table, sizeof(RecvTable), 2) &&
                    image.within(node->network_name, 1, 1),
                "Scene ClientClass fields have invalid provenance");
        const auto name_end = image.base + sections[1].rva + sections[1].size;
        require(std::memchr(node->network_name, 0,
                            static_cast<std::size_t>(name_end - reinterpret_cast<const std::byte*>(
                                                                    node->network_name))) !=
                    nullptr,
                "Scene ClientClass network name is not terminated");
        node = node->next;
    }
    require(!node, "Scene class list count differs from the supported build");
}
}

std::span<const BuildSceneClassRecipe, 20> build_scene_class_manifest() noexcept {
    return classes;
}
BuildSceneClassFailure::BuildSceneClassFailure(std::size_t failed, const BuildSceneClassRecipe& row)
    : std::runtime_error("ClientClass startup could not resolve " + std::string(row.network_name)),
      index(failed), operand(row.operand), class_hash(row.class_hash),
      destination_cell(row.destination_cell), network_name(row.network_name) {}
void initialize_build_scene_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Scene bindings require the complete canonical x86 data image");
    const auto image = checked_image(data.native_cell<std::byte*>(0x4346A55Cu));
    auto* const base_client = data.native_cell<void*>(0x434683E0u);
    validate_class_list(image, base_client);
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const auto& row = classes[i];
        require(row.operand == 0x390u + i * 8u &&
                    netvar_name_hash(row.network_name) == row.class_hash,
                "Scene class recipe identity differs");
        auto* const found = find_client_class(base_client, row.class_hash);
        if (!found)
            throw BuildSceneClassFailure(i, row);
        require(std::strcmp(found->network_name, row.network_name) == 0,
                "Scene ClientClass hash collision differs from the requested name");
        data.native_cell<ClientClass*>(row.destination_cell) = found;
    }
    const auto* breakable = breakable_function(image);
    data.native_cell<EngineBreakableFunction>(0x43468F30u) =
        reinterpret_cast<EngineBreakableFunction>(const_cast<std::byte*>(breakable));
}
} // namespace skeet::engine
