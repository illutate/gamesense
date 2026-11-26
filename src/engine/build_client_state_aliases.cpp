#include "skeet/engine/build_client_state_aliases.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
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
    void code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(rva >= 0x1000u && pattern.size() <= 0x451C32u &&
                    rva - 0x1000u <= 0x451C32u - pattern.size(),
                "Client-state alias code exceeds engine text");
        std::size_t index = 0;
        for (const int value : pattern) {
            require(value < 0 || static_cast<unsigned char>(base[rva + index]) == value,
                    "Client-state alias instruction fingerprint differs");
            ++index;
        }
    }
    std::byte* operand(std::uint32_t rva, std::uint32_t target) const {
        auto* const pointer = base + target;
        require(read<std::uint32_t>(base + rva) == reinterpret_cast<std::uintptr_t>(pointer),
                "Client-state alias instruction operand has different provenance");
        return pointer;
    }
};
Image checked_engine(void* module) {
    require(module != nullptr, "Client-state aliases require the initialized engine module");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported client-state alias DOS/NT-header layout");
    const auto* const nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x64EE7E86 &&
                read<std::uint32_t>(nt + 0x50) == 0x39C0000 &&
                read<std::uint32_t>(nt + 0x58) == 0x64174E,
            "Unsupported client-state alias engine build");
    struct Section {
        char name[8];
        std::uint32_t rva, size, flags;
    };
    constexpr std::array<Section, 2> sections{
        {{".text", 0x1000, 0x451C32, 0x60000020}, {".data", 0x528000, 0x3427638, 0xC0000040}}};
    for (const auto& expected : sections) {
        unsigned matches = 0;
        for (unsigned index = 0; index < 7; ++index) {
            const auto* const section = nt + 0xF8 + index * 40;
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(read<std::uint32_t>(section + 8) == expected.size &&
                        read<std::uint32_t>(section + 12) == expected.rva &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Unsupported client-state alias engine section");
            ++matches;
        }
        require(matches == 1, "Client-state alias engine section is missing or duplicated");
    }
    return {base};
}
}

BuildClientStateAliases initialize_build_client_state_aliases(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Client-state aliases require the original x86 ABI");
    require(data.size() >= NativeDataImage::image_size,
            "Client-state aliases require complete canonical storage");
    const auto image = checked_engine(data.native_cell<void*>(0x43468D20u));
    // The actual getter adds eight to the engine's raw split-screen record.
    // All following member offsets are relative to that raw record in code.
    image.code(0xE4490,
               {0x83, 0xF9, 0xFF, 0x75, 0x18, 0xA1, -1,   -1, -1,   -1,   0xB9, -1,   -1,   -1,
                -1,   0xFF, 0x50, 0x14, 0x8B, 0x04, 0x85, -1, -1,   -1,   -1,   0x83, 0xC0, 8,
                0xC3, 0x8B, 0x04, 0x8D, -1,   -1,   -1,   -1, 0x83, 0xC0, 8,    0xC3});
    image.operand(0xE4496, 0x59F198);
    image.operand(0xE449B, 0x59F198);
    const auto* const records = image.operand(0xE44A5, 0x59F19C);
    image.operand(0xE44B0, 0x59F19C);
    const auto record = read<std::uint32_t>(records);
    require(record && (record & 3u) == 0 &&
                record <= std::numeric_limits<std::uint32_t>::max() - 0x4E70u,
            "Client-state aliases require an initialized current engine record");
    auto* const state = reinterpret_cast<std::byte*>(std::uintptr_t(record + 8u));
    require(data.native_cell<void*>(0x43468E44u) == state,
            "Client-state aliases do not share the canonical current client state");

    image.code(0xC1609,
               {0xA1, -1, -1, -1, -1, 0xC7, 0x80, 0x28, 0x4D, 0, 0, 0,    0,    0,    0,
                0xA1, -1, -1, -1, -1, 0xC7, 0x80, 0x30, 0x4D, 0, 0, 0,    0,    0,    0,
                0xA1, -1, -1, -1, -1, 0xC7, 0x80, 0x2C, 0x4D, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF});
    for (const auto operand : {0xC160Au, 0xC1619u, 0xC1628u})
        image.operand(operand, 0x59F19C);
    image.code(0xB6840, {0xA1, -1, -1, -1, -1, 0x8B, 0x80, 0x6C, 1, 0, 0, 0xC3});
    image.operand(0xB6841, 0x59F19C);
    image.code(0x25E75, {0x8B, 0x35, -1,   -1,   -1,   -1,   0x80, 0xBE, 0x1C, 0x4D, 0,    0,
                         0,    0x66, 0x0F, 0x6E, 0x86, 0x70, 1,    0,    0,    0x0F, 0x5B, 0xC0});
    image.operand(0x25E77, 0x59F19C);

    // The network handshake's sequence is the actual raw client-state delta
    // tick. Independent engine prediction and full-update reset sites load
    // the same current record and consume/reset its+174 member. Native's
    // canonical public client-state pointer names raw+8, hence public+16C.
    image.code(0xE20F0, {0x57, 0x8B, 0x3D, -1,   -1,   -1,   -1, 0x83, 0xBF, 0x08, 1,    0,   0,
                         6,    0x75, 0x4A, 0x83, 0xBF, 0x74, 1,  0,    0,    0,    0x7C, 0x41});
    image.operand(0xE20F3, 0x59F19C);
    image.code(0xE211A, {0x8B, 0x87, 0x30, 0x4D, 0,    0, 0x03, 0x87, 0x2C, 0x4D,
                         0,    0,    0x8B, 0x97, 0x74, 1, 0,    0,    0x85, 0xD2});
    image.code(0xBD15E, {0x8B, 0x35, -1,   -1,   -1,   -1, 0x83, 0xBE, 0x74, 1,  0, 0,
                         0xFF, 0x74, 0x25, 0x8D, 0x4E, 8,  0xE8, -1,   -1,   -1, -1});
    image.operand(0xBD160, 0x59F19C);
    image.code(0xBD17F, {0xC7, 0x86, 0x74, 1, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF});

    // FireEvents and its reset method share the current record's event queue.
    // The queue starts at raw+4E60; its head is at+0C and each event's next
    // link is at+38. Native4337D273 reads this queue through43475A50.
    image.code(0xB7550,
               {0x8B, 0x0D, -1, -1, -1, -1, 0x8D, 0x89, 0x60, 0x4E, 0, 0, 0xE9, 0x3F, 0x1E, 0, 0});
    image.operand(0xB7552, 0x59F19C);
    image.code(0xE0087, {0x8B, 0x1D, -1, -1, -1, -1});
    image.operand(0xE0089, 0x59F19C);
    image.code(0xE0098, {0x8D, 0x8B, 0x60, 0x4E, 0, 0, 0xE8, 0xFD, 0x92, 0xFD, 0xFF});
    image.code(0xE00BA, {0x8B, 0xBB, 0x6C, 0x4E, 0, 0, 0x85, 0xFF});
    image.code(0xE00D0, {0x66, 0x83, 0x3F, 0, 0x8B, 0x47, 0x38});
    image.code(0xE0152, {0x8B, 0x47, 0x0C, 0x8B, 0x40, 4, 0x85, 0xC0});

    auto* const globals = image.base + 0x59EE60;
    require(data.native_cell<void*>(0x43467B04u) == globals,
            "Client-state aliases do not share the canonical engine globals");
    // Frame/time publication proves the common CGlobalVars object. Independent
    // tick-count and max-client stores prove those two member identities.
    image.code(0x2296FE,
               {0xF3, 0x0F, 0x11, 0x1D, -1,   -1,   -1,   -1,   0x89, 0x15, -1,   -1,   -1,
                -1,   0xF3, 0x0F, 0x11, 5,    -1,   -1,   -1,   -1,   0xF3, 0x0F, 0x11, 0x0D,
                -1,   -1,   -1,   -1,   0xF3, 0x0F, 0x11, 0x15, -1,   -1,   -1,   -1,   0xC3});
    image.operand(0x229702, 0x59EE60);
    image.operand(0x229708, 0x59EE64);
    image.operand(0x229710, 0x59EE68);
    image.operand(0x229718, 0x59EE6C);
    image.operand(0x229720, 0x59EE80);
    image.code(0xD9AB9, {0x8B, 0x86, 0x68, 1,  0,  0,    0x8B, 0xCE, 0xA3, -1, -1, -1, -1,
                         0xE8, -1,   -1,   -1, -1, 0xF3, 0x0F, 0x11, 5,    -1, -1, -1, -1});
    image.operand(0xD9AC2, 0x59EE7C);
    image.operand(0xD9ACF, 0x59EE70);
    image.code(0x2864E2, {0x8B, 0x86, 0x80, 3, 0, 0, 0xA3, -1, -1, -1, -1, 0x8B, 0x47, 8, 0xA3, -1,
                          -1, -1, -1});
    image.operand(0x2864E9, 0x59EE78);
    image.operand(0x2864F1, 0x59EE8C);

    // The correction subtracts acknowledged and adds outgoing. The separate
    // publication/reset site must name the same two live engine counters.
    image.code(0x1A3656, {0x2B, 5,    -1,   -1,   -1,   -1,   3,    5,    -1,   -1,  -1,
                          -1,   0x66, 0x0F, 0x6E, 0xD8, 0x33, 0xC0, 0x0F, 0x5B, 0xDB});
    auto* const acknowledged = image.operand(0x1A3658, 0x391CCC8);
    auto* const outgoing = image.operand(0x1A365E, 0x391CCDC);
    image.code(0x22B32E, {0x89, 0x35, -1, -1, -1, -1, 0xC7, 5, -1, -1, -1, -1, 0, 0, 0, 0});
    image.operand(0x22B330, 0x391CCDC);
    image.operand(0x22B336, 0x391CCC8);

    const BuildClientStateAliases result{reinterpret_cast<std::int32_t*>(state + 0x4D28),
                                         reinterpret_cast<std::int32_t*>(state + 0x4D24),
                                         reinterpret_cast<std::int32_t*>(state + 0x164),
                                         reinterpret_cast<std::int32_t*>(state + 0x168),
                                         reinterpret_cast<float*>(globals),
                                         reinterpret_cast<std::int32_t*>(globals + 0x1C),
                                         reinterpret_cast<std::int32_t*>(globals + 0x18),
                                         reinterpret_cast<std::int32_t*>(acknowledged),
                                         reinterpret_cast<std::int32_t*>(outgoing)};
    data.native_cell<std::int32_t*>(0x434757DCu) = result.choked_commands;
    data.native_cell<std::int32_t*>(0x43475B2Cu) = result.last_outgoing_command;
    data.native_cell<std::int32_t*>(0x43475A60u) = result.server_tick;
    data.native_cell<std::int32_t*>(0x434758D8u) = reinterpret_cast<std::int32_t*>(state + 0x16C);
    data.native_cell<void*>(0x43475A50u) = state + 0x4E58;
    data.native_cell<float*>(0x4346A334u) = result.real_time;
    data.native_cell<std::int32_t*>(0x43468D4Cu) = result.tick_count;
    data.native_cell<std::int32_t*>(0x43469300u) = result.maximum_clients;
    data.native_cell<std::int32_t*>(0x43468F9Cu) = result.acknowledged_tick;
    data.native_cell<std::int32_t*>(0x434670A8u) = result.outgoing_tick;
    return result;
}
} // namespace skeet::engine
