#include "skeet/engine/build_entity_animation_bindings.hpp"
#include "skeet/engine/offset_storage.hpp"
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
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
struct Section {
    char name[8];
    std::uint32_t rva, size, flags;
};
constexpr std::array<Section, 3> sections{{{".text", 0x1000, 0xB38409, 0x60000020},
                                           {".rdata", 0xB3A000, 0x26629C, 0x40000040},
                                           {".data", 0xDA1000, 0x460C3E0, 0xC0000040}}};
struct ClientImage {
    std::byte* base;
    bool contains(std::uint32_t rva, std::size_t size, unsigned section) const noexcept {
        const auto& s = sections[section];
        return rva >= s.rva && size <= s.size && rva - s.rva <= s.size - size;
    }
    std::byte* code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(contains(rva, pattern.size(), 0),
                "Entity animation instruction leaves executable text");
        auto* const result = base + rva;
        auto* cursor = result;
        for (const auto value : pattern) {
            require(value < 0 || std::uint8_t(*cursor) == value,
                    "Entity animation instruction fingerprint differs");
            ++cursor;
        }
        return result;
    }
    std::byte* operand(std::uint32_t at, std::uint32_t expected, std::size_t size) const {
        require(contains(at, 4, 0) && contains(expected, size, 2),
                "Entity animation operand has wrong section provenance");
        require(read<std::uint32_t>(base + at) == reinterpret_cast<std::uintptr_t>(base + expected),
                "Entity animation data operand has unsupported target");
        return base + expected;
    }
    void call(std::uint32_t at, std::uint32_t target) const {
        code(at, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(at) + 5 + read<std::int32_t>(base + at + 1) == target,
                "Entity animation setter calls a different engine entry");
    }
};
ClientImage checked(void* module) {
    require(module != nullptr, "Entity animation requires the loaded client.dll");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported entity animation DOS header");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Unsupported entity animation client build");
    std::array<bool, 3> found{};
    for (unsigned i = 0; i < 7; ++i) {
        const auto* section = nt + 0xF8 + i * 40;
        const auto size = read<std::uint32_t>(section + 8), rva = read<std::uint32_t>(section + 12);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva,
                "Entity animation PE section exceeds image");
        for (unsigned j = 0; j < sections.size(); ++j) {
            const auto& expected = sections[j];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[j] && size == expected.size && rva == expected.rva &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Entity animation section fingerprint differs");
            found[j] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Entity animation client section is missing");
    return {base};
}
BuildEntityAnimationBindings acquire(const ClientImage& client) {
    auto* const invalidate =
        client.code(0x1AEC40, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x0C, 0x53,
                               0x8B, 0x5D, 0x08, 0x8B, 0xC3, 0x56, 0x83, 0xE0, 0x04, 0xC6,
                               0x44, 0x24, 0x0B, 0x00, 0x57, 0xC1, 0xE0, 0x0A, 0x8B, 0xF9});
    client.code(0x1AEE67, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0});
    auto* const angles =
        client.code(0x1EA950, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x64, 0x53, 0x56,
                               0x57, 0x8B, 0xF1, 0xE8, 0x5D, 0x08, 0,    0,    0x8B, 0x7D, 0x08,
                               0xF3, 0x0F, 0x10, 0x07, 0x0F, 0x2E, 0x86, 0xC4, 0,    0,    0});
    client.code(0x1EA99F, {0x6A, 0x02, 0x8B, 0xCE});
    client.call(0x1EA9A3, 0x1AEC40);
    client.code(0x1EAA8F, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0});
    auto* const origin =
        client.code(0x1EA790, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x51, 0x53, 0x56, 0x57, 0x8B,
                               0xF1, 0xE8, 0x1F, 0x0A, 0,    0,    0x8B, 0x7D, 0x08, 0xF3, 0x0F,
                               0x10, 0x07, 0x0F, 0x2E, 0x86, 0xA0, 0,    0,    0});
    client.code(0x1EA7DD, {0x6A, 0x01, 0x8B, 0xCE});
    client.call(0x1EA7E1, 0x1AEC40);
    client.code(0x1EA923, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0});
    auto* const bounds = client.code(
        0x28CC90, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF8, 0x83, 0xC4, 0x04, 0x55,
                   0x8B, 0x6B, 0x04, 0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56,
                   0x57, 0x8B, 0x7B, 0x08, 0x8B, 0xD1, 0x8B, 0x4B, 0x0C, 0x8D, 0x72, 0x08});
    client.code(0x28CE3B, {0x5E, 0x8B, 0xE5, 0x5D, 0x8B, 0xE3, 0x5B, 0xC2, 0x08, 0});

    // Paired read/write instructions identify the live byte and double cells.
    // The resource operand remains an outer pointer; its value is never copied.
    client.code(0x1E50D7, {0xA0, -1,   -1,   -1,   -1, 0x0F, 0xB7, 0xCA, 0x66, 0x42, 0x66,
                           0x89, 0x15, -1,   -1,   -1, -1,   0xC6, 0x05, -1,   -1,   -1,
                           -1,   0,    0x88, 0x81, -1, -1,   -1,   -1,   0xC3});
    auto* const transform = client.operand(0x1E50D8, 0xDACC0F, 1);
    client.operand(0x1E50EA, 0xDACC0F, 1);
    client.code(0x3E3809,
                {0xF2, 0x0F, 0x10, 0x0D, -1,   -1,   -1,   -1,   0xF2, 0x0F, 0x10, 0x15, -1,
                 -1,   -1,   -1,   0x0F, 0x28, 0xC1, 0xF2, 0x0F, 0x10, 0x5C, 0x24, 0x20});
    auto* const timestamp = client.operand(0x3E380D, 0x5344B38, 8);
    client.code(0x3E383F, {0xF2, 0x0F, 0x11, 0x1D, -1, -1, -1, -1, 0xE8, 0x04, 0xC6, 0, 0});
    client.operand(0x3E3843, 0x5344B38, 8);
    client.code(0x1F7B6C, {0x8B, 0x35, -1,   -1,   -1,   -1,   0x85, 0xF6, 0x74, 0x25, 0x8B,
                           0x4D, 0x0C, 0x8B, 0xB6, 0xD8, 0x09, 0,    0,    0x83, 0xC1, 0x08,
                           0x8B, 0x01, 0xFF, 0x50, 0x28, 0x8B, 0x0D, -1,   -1,   -1,   -1,
                           0x50, 0x8D, 0x89, 0xD8, 0x09, 0,    0,    0xFF, 0x56, 0x20});
    auto* const resource = client.operand(0x1F7B6E, 0x3231380, 4);
    client.operand(0x1F7B89, 0x3231380, 4);
    // The smoke-handle vector has data at+0 and count at+C. The add/search
    // operation reads both, then passes that same vector to the insert body.
    // Publish its address, preserving initially empty live engine contents.
    client.code(0x3D1700, {0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x1D, -1,   -1,   -1,  -1, 0x56,
                           0x57, 0x33, 0xFF, 0x85, 0xDB, 0x7E, 0x57, 0xA1, -1,  -1, -1,
                           -1,   0x8B, 0x14, 0xB8, 0x83, 0xFA, 0xFF, 0x74, 0x45});
    auto* const smoke = client.operand(0x3D1713, 0x525CAA0, 20);
    client.operand(0x3D1706, 0x525CAAC, 4);
    client.code(0x3D1769, {0x8D, 0x45, 0x08, 0xB9, -1,   -1,   -1,   -1,   0x50, 0x53,
                           0xE8, 0x88, 0x5E, 0xDC, 0xFF, 0x5F, 0x5E, 0x5B, 0x5D, 0xC3});
    client.operand(0x3D176D, 0x525CAA0, 20);
    // Native ragdoll433996EA/4339974E and entity433F9EF1 call this genuine
    // client entry with only ECX. Its two exits both use RET, not RET4.
    // It rebuilds the engine render-eligibility state after source flag edits.
    auto* const visibility =
        client.code(0x1E69D0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x24, 0x53, 0x56,
                               0x57, 0x8B, 0xF9, 0x8D, 0xB7, 0x74, 0x02, 0,    0,    0x85, 0xF6,
                               0x74, 0x06, 0xC7, 0x06, 0,    0,    0,    0,    0x83, 0xEC, 0x08,
                               0x8D, 0x4C, 0x24, 0x18, 0xE8, 0xD6, 0x82, 0x07, 0});
    client.code(0x1E6B01, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3});
    client.code(0x1E6B4F, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3});
    return {reinterpret_cast<MemberFunction<void, std::int32_t>>(invalidate),
            reinterpret_cast<lagcomp::SetAbsoluteVector>(angles),
            reinterpret_cast<lagcomp::SetAbsoluteVector>(origin),
            reinterpret_cast<lagcomp::SetCollisionBounds>(bounds),
            reinterpret_cast<std::uint8_t*>(transform),
            reinterpret_cast<double*>(timestamp),
            reinterpret_cast<void**>(resource),
            smoke,
            reinterpret_cast<MemberFunction<void>>(visibility)};
}
}
BuildEntityAnimationBindings initialize_build_entity_animation_bindings(
    NativeDataImage& data, const LoadedModuleEnvironment& modules, NativeOffsetStorage& offsets) {
    require(sizeof(void*) == 4 && data.size() >= NativeDataImage::image_size,
            "Entity animation binding requires the complete canonical x86 data image");
    require(modules.loader && modules.acquire_peb_lock && modules.release_peb_lock,
            "Entity animation binding requires the loaded-module list and lock imports");
    auto* const module =
        find_loaded_module(modules, loaded_module_name_hash(u"client.dll", -1), true, true);
    require(module && module == data.native_cell<void*>(0x4346A55Cu),
            "Entity animation client differs from the canonical module binding");
    const auto client = checked(module);
    const auto result = acquire(client);
    // The working client encodes these two fields in the keys while both
    // pointers retain word0. Treating the zero word as the decoded offset
    // overwrites the entity vtable during history/animation preparation.
    client.code(0x432580, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x10, 0xF3, 0x0F, 0x10, 0x81, 0xB4, 0,
                           0,    0,    0xF3, 0x0F, 0x58, 0x40, 8,    0xA1, -1,   -1,   -1,   -1,
                           0xF3, 0x0F, 0x11, 0x81, 0x20, 0x99, 0,    0,    0x8B, 0x40, 0x10, 0x89,
                           0x81, 0x24, 0x99, 0,    0,    0x5D, 0xC2, 0x10, 0});
    client.operand(0x432594, 0xDAF8E0, 4);
    client.code(0x432EDA, {0xF3, 0x0F, 0x10, 0x50, 0x10, 0xF3, 0x0F, 0x5C, 0x97, 0x24, 0x99, 0, 0});
    client.code(0x432EFD, {0xF3, 0x0F, 0x10, 0x9F, 0x20, 0x99, 0, 0, 0x0F, 0x2F, 0xC2});
    require(offsets.words && offsets.words[0] == 0 &&
                offsets.globals.pointers.at(0x43467EBCu) == offsets.words &&
                offsets.globals.pointers.at(0x43467A88u) == offsets.words,
            "Collision history fields require their initialized shared offset word");
    auto& collision_height_key = offsets.globals.keys.at(0x43467038u);
    auto& collision_time_key = offsets.globals.keys.at(0x43467160u);
    const auto collision_height = read<std::uint32_t>(client.base + 0x43259C);
    const auto collision_time = read<std::uint32_t>(client.base + 0x4325A5);
    // Interpolation temporarily enables this genuine client byte, then
    // restores the saved byte independently of its neighboring float/time
    // state. Both source query hooks share its live address, not a snapshot.
    client.code(0x1ECB33, {0xA0, -1, -1, -1,   -1,   0x8B, 0xCE, 0xF3, 0x0F, 0x10, 5,  -1,
                           -1,   -1, -1, 0x88, 0x44, 0x24, 8,    0xA1, -1,   -1,   -1, -1});
    auto* const interpolation = client.operand(0x1ECB34, 0x5334C84, 1);
    client.operand(0x1ECB3E, 0x5334C88, 4);
    client.operand(0x1ECB47, 0x5334C7C, 4);
    client.code(0x1ECB60,
                {0xC6, 5,    -1,   -1,   -1,   -1,   1,    0x8B, 0x80, 0x78, 2,    0,    0,
                 0xFF, 0xD0, 0x84, 0xC0, 0x8D, 0x8E, 0xC4, 3,    0,    0,    0x0F, 0x94, 0xC0});
    client.operand(0x1ECB62, 0x5334C84, 1);
    client.code(0x1ECB9A, {0xA2, -1, -1, -1, -1, 0x8B, 0x44, 0x24, 4,  0xF3, 0x0F,
                           0x11, 5,  -1, -1, -1, -1,   0xA3, -1,   -1, -1,   -1});
    client.operand(0x1ECB9B, 0x5334C84, 1);
    client.operand(0x1ECBA7, 0x5334C88, 4);
    client.operand(0x1ECBAC, 0x5334C7C, 4);
    require(
        data.native_cell<void**>(0x43468FF0u) == result.player_resource,
        "Entity animation requires the same published player-resource slot as misc engine bindings");
    collision_height_key = 0x432B1F5Bu ^ collision_height;
    collision_time_key = 0x0D560FACu ^ collision_time;
    data.native_cell<decltype(result.invalidate_physics)>(0x43468C10u) = result.invalidate_physics;
    data.native_cell<decltype(result.set_absolute_angles)>(0x434688C8u) =
        result.set_absolute_angles;
    data.native_cell<decltype(result.set_absolute_origin)>(0x43467E44u) =
        result.set_absolute_origin;
    data.native_cell<decltype(result.set_collision_bounds)>(0x4346A7F0u) =
        result.set_collision_bounds;
    data.native_cell<std::uint8_t*>(0x434683D0u) = result.transform_override;
    data.native_cell<double*>(0x4346A7E4u) = result.animation_timestamp;
    data.native_cell<void*>(0x434679C0u) = result.smoke_registry;
    data.native_cell<decltype(result.update_render_visibility)>(0x43468320u) =
        result.update_render_visibility;
    data.native_cell<std::uint8_t*>(0x43468FA4u) = reinterpret_cast<std::uint8_t*>(interpolation);
    return result;
}
} //namespace skeet::engine
