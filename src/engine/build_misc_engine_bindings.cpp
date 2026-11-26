#include "skeet/engine/build_misc_engine_bindings.hpp"
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
struct Image {
    std::byte* base;
    std::uint32_t text_size, data_rva, data_size;
    void code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(rva >= 0x1000u && pattern.size() <= text_size &&
                    rva - 0x1000u <= text_size - pattern.size(),
                "Misc binding instruction exceeds module text");
        std::size_t index = 0;
        for (const int value : pattern) {
            require(value < 0 || static_cast<unsigned char>(base[rva + index]) == value,
                    "Misc binding instruction fingerprint differs");
            ++index;
        }
    }
    std::byte* operand(std::uint32_t rva, std::uint32_t target) const {
        auto* const pointer = base + target;
        require(read<std::uint32_t>(base + rva) == reinterpret_cast<std::uintptr_t>(pointer),
                "Misc binding operand has different module provenance");
        return pointer;
    }
    void call(std::uint32_t rva, std::uint32_t target) const {
        require(base[rva] == std::byte{0xE8} &&
                    rva + 5u + static_cast<std::uint32_t>(read<std::int32_t>(base + rva + 1)) ==
                        target,
                "Misc binding relative helper identity differs");
    }
};
Image checked_image(void* module, bool client) {
    require(module != nullptr, "Misc bindings require initialized client and engine modules");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported misc binding DOS/NT-header layout");
    const auto* const nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == (client ? 0x65272025u : 0x64EE7E86u) &&
                read<std::uint32_t>(nt + 0x50) == (client ? 0x5509000u : 0x39C0000u) &&
                read<std::uint32_t>(nt + 0x58) == (client ? 0xFA635Fu : 0x64174Eu),
            "Unsupported misc binding client/engine build");
    const Image image{base, client ? 0xB38409u : 0x451C32u, client ? 0xDA1000u : 0x528000u,
                      client ? 0x460C3E0u : 0x3427638u};
    unsigned text = 0, data = 0;
    for (unsigned index = 0; index < 7; ++index) {
        const auto* const section = nt + 0xF8 + index * 40;
        if (std::memcmp(section, ".text\0\0\0", 8) == 0) {
            require(read<std::uint32_t>(section + 8) == image.text_size &&
                        read<std::uint32_t>(section + 12) == 0x1000 &&
                        read<std::uint32_t>(section + 36) == 0x60000020,
                    "Unsupported misc binding text section");
            ++text;
        }
        if (std::memcmp(section, ".data\0\0\0", 8) == 0) {
            require(read<std::uint32_t>(section + 8) == image.data_size &&
                        read<std::uint32_t>(section + 12) == image.data_rva &&
                        read<std::uint32_t>(section + 36) == 0xC0000040,
                    "Unsupported misc binding data section");
            ++data;
        }
    }
    require(text == 1 && data == 1, "Misc binding module section is missing or duplicated");
    return image;
}
}

BuildMiscEngineBindings initialize_build_misc_engine_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Misc engine bindings require the original x86 ABI");
    require(data.size() >= NativeDataImage::image_size,
            "Misc bindings require complete canonical storage");
    const auto client = checked_image(data.native_cell<void*>(0x4346A55Cu), true);
    const auto engine = checked_image(data.native_cell<void*>(0x43468D20u), false);
    // ECX and EDX are saved as the first/second strings before either helper
    // call. The body forwards both into the real ClanTagChanged KeyValues.
    engine.code(0x8DAB0,
                {0x53, 0x56, 0x57, 0x8B, 0xDA, 0x8B, 0xF9, 0xFF, 0x15, -1,   -1,   -1,   -1,   0x6A,
                 0x2C, 0x8B, 0xC8, 0x8B, 0x30, 0xFF, 0x56, 8,    0x85, 0xC0, 0x74, 0x13, 0x51, 0x6A,
                 0,    0x68, -1,   -1,   -1,   -1,   0x8B, 0xC8, 0xE8, -1,   -1,   -1,   -1,   0x8B,
                 0xF0, 0xEB, 2,    0x33, 0xF6, 0x6A, 1,    0x68, -1,   -1,   -1,   -1,   0x8B, 0xCE,
                 0xE8, -1,   -1,   -1,   -1,   0x85, 0xC0, 0x74, 8,    0x57, 0x8B, 0xC8, 0xE8, -1,
                 -1,   -1,   -1,   0x6A, 1,    0x68, -1,   -1,   -1,   -1,   0x8B, 0xCE, 0xE8, -1,
                 -1,   -1,   -1,   0x85, 0xC0, 0x74, 8,    0x53, 0x8B, 0xC8, 0xE8, -1,   -1,   -1,
                 -1,   0xA1, -1,   -1,   -1,   -1,   0xB9, -1,   -1,   -1,   -1,   0x56, 0xFF, 0x90,
                 0xEC, 2,    0,    0,    0x5F, 0x5E, 0x5B, 0xC3});
    engine.operand(0x8DAB9, 0x4537E4);
    const auto* const event_name = engine.operand(0x8DACE, 0x478410);
    const auto* const tag_name = engine.operand(0x8DAE2, 0x478420);
    const auto* const name_name = engine.operand(0x8DAFC, 0x47BD5C);
    require(std::memcmp(event_name, "ClanTagChanged", 15) == 0 &&
                std::memcmp(tag_name, "tag", 4) == 0 && std::memcmp(name_name, "name", 5) == 0,
            "Clan-tag engine message strings differ");
    engine.operand(0x8DB14, 0x59D3E8);
    engine.operand(0x8DB19, 0x59D3E8);
    engine.call(0x8DAD4, 0x2FE940);
    engine.call(0x8DAE8, 0x2FF6B0);
    engine.call(0x8DAF4, 0x300080);
    engine.call(0x8DB02, 0x2FF6B0);
    engine.call(0x8DB0E, 0x300080);

    // The hit-marker sound suppression list is the engine's real64-player
    // array. Its writer bounds the copy to64 entries, and an independent
    // sound-channel consumer reads the same array and count. A valid empty
    // list is retained through this address, not replaced by a null view.
    engine.code(0x242D0,
                {0x55, 0x8B, 0xEC, 0x56, 0xBE, 0x40, 0,    0,    0,    0x39, 0x75, 0x0C, 0x0F,
                 0x46, 0x75, 0x0C, 0x8D, 0x04, 0xB5, 0,    0,    0,    0,    0x50, 0xFF, 0x75,
                 8,    0x68, -1,   -1,   -1,   -1,   0xE8, -1,   -1,   -1,   -1,   0x83, 0xC4,
                 0x0C, 0x89, 0x35, -1,   -1,   -1,   -1,   0x5E, 0x5D, 0xC2, 8,    0});
    auto* const sound_players = engine.operand(0x242EC, 0x38FDCC8);
    engine.operand(0x242FA, 0x391AFA8);
    engine.call(0x242F0, 0x3FB9E0);
    engine.code(0x3A087,
                {0x8B, 0x0D, -1,   -1, -1, -1, 0x33, 0xC0, 0x85, 0xC9, 0x7E, 0x11, 0x8B, 0x56, 4,
                 0x3B, 0x14, 0x85, -1, -1, -1, -1,   0x74, 0x51, 0x40, 0x3B, 0xC1, 0x7C, 0xF2});
    engine.operand(0x3A089, 0x391AFA8);
    engine.operand(0x3A099, 0x38FDCC8);
    // Performance-owner slot1 retains the operand word at+4 of these actual
    // engine clock instructions. Keep the instruction records themselves;
    // the native constructor does not receive a snapshot of either clock.
    engine.code(0x8E550, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0x9C, 0,    0,
                          0,    0xF2, 0x0F, 0x10, 5,    -1,   -1,   -1,   -1,   0x8D, 0x44,
                          0x24, 0x1C, 0x56, 0x6A, 0x20, 0xF2, 0x0F, 0x11, 0x41, 0x60});
    engine.operand(0x8E560, 0x391DA98);
    engine.code(0x22BE2E, {0xF2, 0x0F, 0x10, 0x45, 0xA0, 0x0F, 0x28, 0xC8, 0xF2, 0x0F, 0x5C,
                           0x0D, -1,   -1,   -1,   -1,   0xF2, 0x0F, 0x11, 5,    -1,   -1,
                           -1,   -1,   0xF2, 0x0F, 0x10, 0x45, 0xA8, 0x66, 0x0F, 0x2F, 0xC8});
    engine.operand(0x22BE3A, 0x391DA68);
    engine.operand(0x22BE42, 0x391DA68);

    client.code(0x195E80, {0x55, 0x8B, 0xEC, 0x83, 0x3D, -1, -1, -1, -1, 0, 0x74, 0x2A});
    auto* const game_rules = client.operand(0x195E85, 0x5334764);
    client.code(0x196BA3, {0x8B, 0x0D, -1,   -1, -1,   -1,   0x85, 0xC9, 0x0F, 0x84, 3,    1,
                           0,    0,    0x8B, 1,  0x8B, 0x80, 0x84, 0,    0,    0,    0xFF, 0xD0});
    client.operand(0x196BA5, 0x5334764);
    client.code(0x1B430E, {0xA1, -1,   -1, -1,   -1, 0x89, 0x44, 0x24, 0x60, 0x85, 0xC0,
                           0x74, 0x3B, 5,  0xD8, 9,  0,    0,    0x74, 0x34, 0x8B, 0x30,
                           0x83, 0xC1, 8,  0x8B, 1,  0x8B, 0x40, 0x28, 0xFF, 0xD0});
    auto* const player_resource = client.operand(0x1B430F, 0x3231380);
    // Both the element pointer and signed element count are loaded from the
    // same CUtlVector header; no current planted entity is captured here.
    client.code(0x3F6510, {0x55, 0x8B, 0xEC, 0x8B, 0x15, -1,   -1,   -1,   -1,   0x33, 0xC9,
                           0x56, 0x85, 0xD2, 0x7E, 0x1A, 0x8B, 0x45, 8,    0x8B, 0x35, -1,
                           -1,   -1,   -1,   0x8B, 0,    0x0F, 0x1F, 0x44, 0,    0,    0x39,
                           4,    0x8E, 0x74, 0x0C, 0x41, 0x3B, 0xCA, 0x7C, 0xF6});
    client.operand(0x3F6515, 0x525D44C);
    auto* const planted_bombs = client.operand(0x3F6525, 0x525D440);
    client.code(0x3F54CA, {0x8B, 0x0D, -1, -1, -1, -1,   0x33, 0xC0, 0x85, 0xC9, 0x7E, 0x2C, 0x8B,
                           0x15, -1,   -1, -1, -1, 0x0F, 0x1F, 0x40, 0,    0x39, 0x34, 0x82});
    client.operand(0x3F54CC, 0x525D44C);
    client.operand(0x3F54D8, 0x525D440);

    // Independently follow the real SetLocalPlayerReady registration, its
    // JavaScript wrapper and the one-string bridge to the actual stdcall
    // engine operation. The old static loader VA is not a live callback.
    client.code(0x6041DC,
                {0x68, -1, -1, -1, -1, 0x50, 0x8D, 0x45, 0xF0, 0x50, 0xFF, 0x15, -1, -1, -1, -1});
    client.operand(0x6041DD, 0x605E10);
    client.code(0x604208, {0x68, -1, -1, -1, -1, 0x50, 0x8D, 0x45, 0xEC, 0x50});
    auto* const ready_name = client.operand(0x604209, 0xC71A1C);
    require(std::memcmp(ready_name, "SetLocalPlayerReady", 20) == 0,
            "Auto-accept JavaScript registration name differs");
    client.code(0x604239, {0x68, -1, -1, -1, -1, 0xFF, 0x90, 0x94, 2, 0, 0});
    client.operand(0x60423A, 0xC71A1C);
    client.code(0x605F41, {0x8D, 0x44, 0x24, 0x0C, 0x8B, 0xD7, 0x50, 0x8B, 0xCE});
    client.call(0x605F4A, 0x606D30);
    client.code(0x606D30, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 8, 0x53, 0x56, 0x8B, 0xF2, 0xFF, 0x30});
    client.call(0x606D3C, 0x601760);
    client.code(0x606D41,
                {0x8B, 0x0D, -1, -1, -1, -1, 0x8A, 0xD8, 0x8B, 0x11, 0xFF, 0x92, 4, 2, 0, 0});
    client.code(0x601760,
                {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x4D, 8,    0xBA, -1,   -1,   -1,   -1,
                 0xE8, -1,   -1,   -1,   -1,   0x85, 0xC0, 0x75, 0x12, 0x8D, 0x50, 2,    0x33, 0xC9,
                 0xE8, -1,   -1,   -1,   -1,   0xB0, 1,    0x8B, 0xE5, 0x5D, 0xC2, 4,    0,    0x83,
                 0x3D, -1,   -1,   -1,   -1,   0,    0x74, 0x0C, 0x51, 0xE8, -1,   -1,   -1,   -1,
                 0x8B, 0xE5, 0x5D, 0xC2, 4,    0,    0x32, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 4,    0});
    auto* const deferred = client.operand(0x60176A, 0xC72164);
    require(std::memcmp(deferred, "deferred", 9) == 0,
            "Auto-accept deferred operation name differs");
    client.operand(0x60178B, 0x5344D68);
    client.call(0x60176E, 0x9962C0);
    client.call(0x60177C, 0x425580);
    client.call(0x601793, 0x425E70);

    const BuildMiscEngineBindings result{
        reinterpret_cast<misc::SetClanTag>(engine.base + 0x8DAB0),
        reinterpret_cast<void**>(game_rules), reinterpret_cast<void**>(player_resource),
        reinterpret_cast<const misc::AutomaticWeaponsBombList*>(planted_bombs)};
    data.native_cell<misc::SetClanTag>(0x43467550u) = result.set_clan_tag;
    data.native_cell<void**>(0x4346A3A4u) = result.game_rules;
    data.native_cell<void**>(0x43468FF0u) = result.player_resource;
    data.native_cell<const misc::AutomaticWeaponsBombList*>(0x434688C0u) = result.planted_bombs;
    data.native_cell<std::int32_t*>(0x4346D558u) = reinterpret_cast<std::int32_t*>(sound_players);
    data.native_cell<void*>(0x4346D554u) = client.base + 0x601760;
    data.native_cell<void*>(0x4346D560u) = engine.base + 0x8E55C;
    data.native_cell<void*>(0x4346D55Cu) = engine.base + 0x22BE36;
    return result;
}
} // namespace skeet::engine
