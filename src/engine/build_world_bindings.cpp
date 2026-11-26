#include "skeet/engine/build_world_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* p) {
    T result;
    std::memcpy(&result, p, sizeof result);
    return result;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Region {
    std::uint32_t rva, size, flags;
};
struct Profile {
    std::uint32_t nt, timestamp, size, checksum;
    std::uint16_t sections;
    std::array<Region, 3> regions;
};
constexpr Profile client_profile{0x150,
                                 0x65272025,
                                 0x5509000,
                                 0xFA635F,
                                 7,
                                 {{{0x1000, 0xB38409, 0x60000020},
                                   {0xB3A000, 0x26629C, 0x40000040},
                                   {0xDA1000, 0x460C3E0, 0xC0000040}}}};
constexpr Profile panorama_profile{0x160,
                                   0x651F4636,
                                   0x2C6000,
                                   0x26E92E,
                                   8,
                                   {{{0x1000, 0x1E5C8D, 0x60000020},
                                     {0x1E7000, 0x51D7A, 0x40000040},
                                     {0x239000, 0x65DD8, 0xC0000040}}}};
struct Image {
    std::byte* base;
    const Profile& profile;
    bool contains(std::uint32_t at, std::size_t length, unsigned kind) const {
        const auto& r = profile.regions[kind];
        return at >= r.rva && length <= r.size && at - r.rva <= r.size - length;
    }
    void code(std::uint32_t at, std::initializer_list<int> bytes) const {
        require(contains(at, bytes.size(), 0), "World binding code exceeds supported module text");
        for (const int value : bytes) {
            require(value < 0 || std::uint8_t(base[at]) == value,
                    "World binding instruction/ABI differs");
            ++at;
        }
    }
    std::byte* operand(std::uint32_t at, std::uint32_t target, std::size_t size,
                       unsigned kind) const {
        require(contains(at, 4, 0) && contains(target, size, kind),
                "World binding operand has wrong provenance");
        require(read<std::uint32_t>(base + at) == reinterpret_cast<std::uintptr_t>(base + target),
                "World binding operand reaches another module object");
        return base + target;
    }
    void pointer(std::uint32_t at, std::uint32_t target, std::size_t size, unsigned kind) const {
        require(contains(at, 4, 1) && contains(target, size, kind),
                "World RTTI pointer has wrong provenance");
        require(read<std::uint32_t>(base + at) == reinterpret_cast<std::uintptr_t>(base + target),
                "World RTTI pointer identity differs");
    }
    void call(std::uint32_t at, std::uint32_t target) const {
        code(at, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(at) + 5 + read<std::int32_t>(base + at + 1) == target,
                "World constructor/getter calls another engine body");
    }
};
Image checked(void* pointer, const Profile& profile) {
    require(pointer != nullptr, "World bindings require acquired client and panorama modules");
    auto* base = static_cast<std::byte*>(pointer);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == profile.nt,
            "World binding DOS/NT layout differs");
    const auto* nt = base + profile.nt;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == profile.sections &&
                read<std::uint16_t>(nt + 20) == 0xE0 && read<std::uint16_t>(nt + 24) == 0x10B &&
                read<std::uint32_t>(nt + 8) == profile.timestamp &&
                read<std::uint32_t>(nt + 80) == profile.size &&
                read<std::uint32_t>(nt + 88) == profile.checksum,
            "Unsupported world binding module build");
    std::array<bool, 3> found{};
    for (unsigned i = 0; i < profile.sections; ++i) {
        const auto* s = nt + 0xF8 + i * 40;
        const auto rva = read<std::uint32_t>(s + 12), size = read<std::uint32_t>(s + 8);
        require(rva <= profile.size && size <= profile.size - rva,
                "World module section exceeds image");
        for (unsigned kind = 0; kind < found.size(); ++kind) {
            constexpr std::array<const char*, 3> names{".text\0\0", ".rdata\0", ".data\0\0"};
            if (std::memcmp(s, names[kind], 8))
                continue;
            const auto& expected = profile.regions[kind];
            require(!found[kind] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(s + 36) == expected.flags,
                    "World module section profile differs");
            found[kind] = true;
        }
    }
    require(found[0] && found[1] && found[2], "World binding module is missing a required section");
    return {base, profile};
}
}
void initialize_build_map_game_system_bindings(NativeDataImage& data) {
    const auto client = checked(data.native_cell<void*>(0x4346A55Cu), client_profile);
    struct List {
        std::uint32_t constructor, append, size, begin, cell;
    };
    constexpr std::array lists{List{0x29628u, 0x2F2FC0u, 0xDF14C0u, 0xDF14B4u, 0x43468D0Cu},
                               List{0x29658u, 0x2F3090u, 0xDF14D4u, 0xDF14C8u, 0x43467A70u}};
    std::array<std::int32_t*, 2> sizes{};
    for (std::size_t index = 0; index < lists.size(); ++index) {
        const auto& list = lists[index];
        client.code(list.constructor, {0xC7, 5, -1, -1, -1, -1, 0, 0, 0, 0, 0xA3, -1, -1, -1, -1});
        sizes[index] =
            reinterpret_cast<std::int32_t*>(client.operand(list.constructor + 2, list.size, 4, 2));
        client.operand(list.constructor + 11, list.begin + 0x10, 4, 2);
        client.code(list.append, {0x55, 0x8B, 0xEC, 0x8B, 0x15, -1,   -1,   -1, -1,   0x8B,
                                  0x0D, -1,   -1,   -1,   -1,   0x8D, 0x42, 1,  0x3B, 0xC1});
        client.operand(list.append + 5, list.size, 4, 2);
        client.operand(list.append + 11, list.begin + 4, 4, 2);
        client.code(list.append + 0x25,
                    {0xA1, -1, -1, -1, -1, 0x42, 0x56, 0x8B, 0x75, 8, 0x89, 0x15, -1, -1, -1, -1});
        client.operand(list.append + 0x26, list.begin, 0x14, 2);
        client.operand(list.append + 0x31, list.size, 4, 2);
    }
    for (std::size_t index = 0; index < lists.size(); ++index)
        data.native_cell<std::int32_t*>(lists[index].cell) = sizes[index];
}
BuildWorldBindings initialize_build_world_bindings(NativeDataImage& data) {
    const auto client = checked(data.native_cell<void*>(0x4346A55C), client_profile);
    const auto panorama = checked(data.native_cell<void*>(0x43469234), panorama_profile);
    // ClientModeCSNormal: actual CRT initializer, constructor stores, live
    // inline vptr and COL/type identity all agree on the same object.
    client.code(0x3F390, {0xB9, -1, -1, -1, -1});
    client.call(0x3F395, 0x3F80A0);
    auto* const mode = client.operand(0x3F391, 0x525E110, 0x34, 2);
    client.code(0x3F8121, {0xC7, 6, -1, -1, -1, -1, 0xC7, 0x46, 4, -1, -1, -1, -1});
    client.operand(0x3F8123, 0xC04980, 0x94, 1);
    client.operand(0x3F812A, 0xC04A70, 4, 1);
    require(read<std::uint32_t>(mode) == reinterpret_cast<std::uintptr_t>(client.base + 0xC04980),
            "ClientModeCSNormal has not been initialized by its real constructor");
    client.pointer(0xC0497C, 0xD61D9C, 20, 1);
    client.pointer(0xD61DA8, 0xE25B60, 32, 2);
    constexpr char type_name[] = ".?AVClientModeCSNormal@@";
    require(read<std::uint32_t>(client.base + 0xD61DA0) == 0 &&
                std::memcmp(client.base + 0xE25B68, type_name, sizeof type_name) == 0,
            "ClientModeCSNormal RTTI differs");
    client.code(0x263150, {0x55, 0x8B, 0xEC, 0xA1, -1, -1, -1, -1, 0xB9, -1, -1, -1, -1});
    client.operand(0x263154, 0x525E110, 4, 2);
    client.operand(0x263159, 0x525E110, 4, 2);
    client.code(0x263165, {0x8B, 0x80, 0x90, 0, 0, 0, 0x57, 0x56, 0xFF, 0xD0});

    // The lazy engine getter returns an object through this slot. Borrow the
    // slot, including its native null state; do not run/skip its allocator.
    client.code(0x27A4A0, {0xA1, -1, -1, -1, -1, 0x85, 0xC0, 0x75, 0x2E, 0xA1});
    auto** const rank = reinterpret_cast<void**>(client.operand(0x27A4A1, 0x5334848, 4, 2));
    client.code(0x27A4AE, {0x68, 0x18, 0x6E, 0, 0, 0x8B, 8, 0x8B, 1, 0xFF, 0x50, 4, 0x85, 0xC0,
                           0x74, 0x0D, 0x8B, 0xC8});
    client.call(0x27A4C0, 0x5A93C0);
    client.code(0x27A4C5, {0xA3, -1, -1, -1, -1, 0xC3, 0xC7, 5, -1, -1, -1, -1, 0, 0, 0, 0, 0x33,
                           0xC0, 0xC3});
    client.operand(0x27A4C6, 0x5334848, 4, 2);
    client.operand(0x27A4CD, 0x5334848, 4, 2);

    client.code(0x32110, {0xA1, -1, -1, -1, -1, 0xA3, -1, -1, -1, -1,  0xC7,
                          5,    -1, -1, -1, -1, -1,   -1, -1, -1, 0xC3});
    auto** const head =
        reinterpret_cast<GameSystemRegistrationNode**>(client.operand(0x32111, 0x5334E08, 4, 2));
    client.operand(0x32116, 0xDF7AE0, 4, 2);
    client.operand(0x3211C, 0x5334E08, 4, 2);
    client.operand(0x32120, 0xDF7AD8, 12, 2);
    client.code(0x35FD40, {0x56, 0x8B, 0x35, -1, -1, -1, -1, 0x85, 0xF6, 0x74, 0x18});
    client.operand(0x35FD43, 0x5334E08, 4, 2);
    client.code(0x35FD50, {0x8B, 0x4E, 4,    0x85, 0xC9, 0x74, 5,    0x8B, 1,    0xFF, 0x50,
                           4,    0x8B, 0x76, 8,    0x85, 0xF6, 0x75, 0xED, 0x5E, 0xC3});

    // The complete push/pop pair shares the genuine counter and panel stack.
    // Begin uses RET4; the typed source caller must not clean that word again.
    panorama.code(0x3DFE0, {0x55, 0x8B, 0xEC, 0x8B, 0x0D, -1, -1, -1, -1, 0x83, 0xF9, 0x40, 0x7C,
                            0x12, 0x68});
    panorama.operand(0x3DFE5, 0x286FD0, 4, 2);
    panorama.operand(0x3DFEF, 0x1FC660, 1, 1);
    panorama.code(0x3DFF3, {0xFF, 0x15, -1, -1, -1, -1, 0x83, 0xC4, 4, 0x5D, 0xC2, 4, 0});
    panorama.operand(0x3DFF5, 0x1E72D8, 4, 1);
    panorama.code(0x3E000, {0x8B, 0x45, 8,  0x41, 0x89, 0x0D, -1,   -1,   -1, -1, 0x89,
                            4,    0x8D, -1, -1,   -1,   -1,   0x5D, 0xC2, 4,  0});
    panorama.operand(0x3E006, 0x286FD0, 4, 2);
    panorama.operand(0x3E00D, 0x274388, 65 * 4, 2);
    panorama.code(0x3E020, {0xA1, -1, -1, -1, -1, 0x85, 0xC0, 0x7E, 0x11, 0xC7, 4,  0x85, -1,  -1,
                            -1,   -1, 0,  0,  0,  0,    0x48, 0xA3, -1,   -1,   -1, -1,   0xC3});
    panorama.operand(0x3E021, 0x286FD0, 4, 2);
    panorama.operand(0x3E02C, 0x274388, 65 * 4, 2);
    panorama.operand(0x3E036, 0x286FD0, 4, 2);
    BuildWorldBindings result{
        data,
        mode,
        rank,
        head,
        reinterpret_cast<script::PanoramaBeginEngineCall>(panorama.base + 0x3DFE0),
        reinterpret_cast<script::PanoramaEndEngineCall>(panorama.base + 0x3E020)};
    // Publish only after both modules and every real dependency have passed.
    data.native_cell<void*>(0x4346D664) = result.rank_context;
    data.native_cell<void**>(0x43468250) = result.rank_data;
    data.native_cell<GameSystemRegistrationNode**>(0x43468D50) = result.registry_head;
    data.native_cell<script::PanoramaBeginEngineCall>(0x4346D628) = result.begin_engine_call;
    data.native_cell<script::PanoramaEndEngineCall>(0x4346D62C) = result.end_engine_call;
    return result;
}
} //namespace skeet::engine
