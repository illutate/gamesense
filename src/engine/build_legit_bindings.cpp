#include "skeet/engine/build_legit_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
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
void require(bool value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
}
struct Section {
    const char* name;
    std::uint32_t rva, size, flags;
};
constexpr std::array<Section, 3> sections{{{".text\0\0", 0x1000, 0xB38409, 0x60000020},
                                           {".rdata\0", 0xB3A000, 0x26629C, 0x40000040},
                                           {".data\0\0", 0xDA1000, 0x460C3E0, 0xC0000040}}};
struct Image {
    std::byte* base;
    bool contains(std::uint32_t rva, std::size_t length, unsigned kind) const noexcept {
        const auto& s = sections[kind];
        return rva >= s.rva && length <= s.size && rva - s.rva <= s.size - length;
    }
    void code(std::uint32_t rva, std::initializer_list<int> expected) const {
        require(contains(rva, expected.size(), 0), "LEGIT smoke code leaves client text");
        for (const auto value : expected) {
            require(value < 0 || std::uint8_t(base[rva]) == value,
                    "LEGIT smoke instruction fingerprint differs");
            ++rva;
        }
    }
    void operand(std::uint32_t rva, std::uint32_t target, unsigned kind, std::size_t length) const {
        require(contains(rva, 4, 0) && contains(target, length, kind) &&
                    read<std::uint32_t>(base + rva) ==
                        reinterpret_cast<std::uintptr_t>(base + target),
                "LEGIT smoke operand has different module/section provenance");
    }
};
Image checked(NativeDataImage& data) {
    auto* base = data.native_cell<std::byte*>(0x4346A55Cu);
    require(base != nullptr, "LEGIT smoke requires the actual acquired client module");
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported LEGIT smoke DOS/NT layout");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Unsupported LEGIT smoke client PE32 build");
    std::array<bool, 3> found{};
    for (unsigned row = 0; row < 7; ++row) {
        const auto* s = nt + 0xF8 + 40 * row;
        const auto rva = read<std::uint32_t>(s + 12), size = read<std::uint32_t>(s + 8);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva,
                "LEGIT smoke client section exceeds image");
        for (unsigned i = 0; i < sections.size(); ++i)
            if (std::memcmp(s, sections[i].name, 8) == 0) {
                require(!found[i] && rva == sections[i].rva && size == sections[i].size &&
                            read<std::uint32_t>(s + 36) == sections[i].flags,
                        "LEGIT smoke section fingerprint differs");
                found[i] = true;
            }
    }
    require(found[0] && found[1] && found[2], "LEGIT smoke requires its actual client sections");
    return {base};
}
std::int32_t entity_index(void*, BallisticEntity entity) {
    return invoke<std::int32_t>(reinterpret_cast<std::byte*>(std::uintptr_t(entity)) + 8, 10);
}
bool through_smoke(void* context, const legit::Vector& first, const legit::Vector& last) {
    return line_passes_through_smoke(*static_cast<NativeDataImage*>(context), first, last);
}
}
BuildLegitBindings initialize_build_legit_bindings(NativeDataImage& data) {
    const auto image = checked(data);
    image.code(0x3D1820, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x15, -1,   -1,   -1,
                          -1,   0x0F, 0x57, 0xC0, 0x56, 0x57, 0x33, 0xFF, 0xF3, 0x0F, 0x11,
                          0x45, 0xFC, 0x85, 0xD2, 0x0F, 0x8E, 0xAF, 0,    0,    0});
    image.operand(0x3D1828, 0x525CAAC, 2, 4);
    image.code(0x3D1840,
               {0x83, 0x3D, -1,   -1,   -1,   -1,   0,    0x0F, 0x84, 0x99, 0,    0,    0,    0xA1,
                -1,   -1,   -1,   -1,   0x8B, 0x0C, 0xB8, 0x83, 0xF9, 0xFF, 0x0F, 0x84, 0x88, 0,
                0,    0,    0x0F, 0xB7, 0xC1, 0xC1, 0xE0, 0x04, 0x05, -1,   -1,   -1,   -1});
    image.operand(0x3D1842, 0x5334764, 2, 4);
    image.operand(0x3D184E, 0x525CAA0, 2, 16);
    image.operand(0x3D1865, 0x4E051CC, 2, 16);
    image.code(0x3D1887, {0xF3, 0x0F, 0x7E, 0x45, 0x14, 0x83, 0xEC, 0x0C, 0x8B, 0x4D, 0x1C,
                          0x8B, 0xD4, 0x83, 0xEC, 0x0C, 0x8B, 0xF0, 0x8B, 0x45, 0x10});
    image.code(0x3D18C4, {0xE8, -1, -1, -1, -1, 0x0F, 0x2E, 0x05, -1, -1, -1, -1, 0x9F, 0xF6, 0xC4,
                          0x44, 0x7B, 0x2B});
    require(std::int64_t(0x3D18C9) + read<std::int32_t>(image.base + 0x3D18C5) == 0x418AB0,
            "LEGIT smoke geometric helper call differs");
    image.code(0x418AB0, {0x55, 0x8B, 0xEC});
    image.operand(0x3D18CC, 0xD2ED04, 1, 4);
    image.code(0x3D18EF,
               {0x33, 0xC0, 0x0F, 0x2F, 0x05, -1,   -1,   -1,   -1,   0x0F, 0x97, 0xC0, 0x5F,
                0x5E, 0x8B, 0xE5, 0x5D, 0xC3, 0x5F, 0xB0, 0x01, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    image.operand(0x3D18F4, 0xD2E9D8, 1, 4);
    data.native_cell<LineSmokeEntry>(0x4346A82Cu) =
        reinterpret_cast<LineSmokeEntry>(image.base + 0x3D1820);
    return {data};
}
legit::TriggerTraceBindings BuildLegitBindings::trigger_trace() const noexcept {
    return {&data, entity_index, through_smoke};
}
bool line_passes_through_smoke(const NativeDataImage& data, const legit::Vector& first,
                               const legit::Vector& last) {
    const auto entry = data.native_cell<LineSmokeEntry>(0x4346A82Cu);
    if (!entry)
        return false; //433E7C94..9B, exact native optional-entry gate
    return entry(first[0], first[1], first[2], last[0], last[1], last[2], 1);
}
} //namespace skeet::engine
