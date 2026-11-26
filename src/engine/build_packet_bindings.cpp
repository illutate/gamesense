#include "skeet/engine/build_packet_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <span>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T result;
    std::memcpy(&result, pointer, sizeof result);
    return result;
}
void require(bool value, const char* reason) {
    if (!value)
        throw std::invalid_argument(reason);
}
struct Section {
    char name[8];
    std::uint32_t rva, size, flags;
};
constexpr std::array<Section, 3> sections{{{".text", 0x1000, 0x451C32, 0x60000020},
                                           {".rdata", 0x453000, 0xD4766, 0x40000040},
                                           {".data", 0x528000, 0x3427638, 0xC0000040}}};
struct Image {
    std::byte* base;
    bool contains(const void* pointer, std::size_t length, std::size_t index) const noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(base) + sections[index].rva;
        return address >= begin && length <= sections[index].size &&
               address - begin <= sections[index].size - length;
    }
    std::uint32_t rva(const void* pointer) const noexcept {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) -
                                          reinterpret_cast<std::uintptr_t>(base));
    }
};
Image checked_image(void* module) {
    require(module != nullptr, "Packet query requires the initialized engine module");
    auto* base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported packet query engine DOS/NT layout");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x64EE7E86 &&
                read<std::uint32_t>(nt + 0x50) == 0x39C0000 &&
                read<std::uint32_t>(nt + 0x58) == 0x64174E,
            "Unsupported packet query engine PE32 build");
    std::array<bool, 3> found{};
    for (std::size_t index = 0; index < 7; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto size = read<std::uint32_t>(section + 8), rva = read<std::uint32_t>(section + 12);
        require(rva <= 0x39C0000 && size <= 0x39C0000 - rva,
                "Packet engine section exceeds its image");
        for (std::size_t wanted = 0; wanted < sections.size(); ++wanted) {
            const auto& expected = sections[wanted];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[wanted] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Unsupported packet query section fingerprint");
            found[wanted] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Packet engine section is missing");
    return {base};
}
bool matches(const std::byte* code, std::span<const int> pattern) noexcept {
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] >= 0 && static_cast<unsigned char>(code[i]) != pattern[i])
            return false;
    return true;
}
void expect(const Image& image, const std::byte* code, std::initializer_list<int> pattern) {
    require(image.contains(code, pattern.size(), 0) &&
                matches(code, {pattern.begin(), pattern.size()}),
            "Packet instruction fingerprint differs");
}
const std::byte* acquire(const Image& image) {
    constexpr std::array pattern{
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0xB8, 0,    0,    0,    0xF3, 0x0F, 0x10,
        0x35, -1,   -1,   -1,   -1,   0x8B, 0xC2, 0x56, 0x57, 0x8B, 0xF9, 0xF3, 0x0F, 0x11, 0x5C,
        0x24, 0x18, 0x8B, 0x0D, -1,   -1,   -1,   -1,   0x89, 0x44, 0x24, 0x5C, 0xF3, 0x0F, 0x10,
        0x57, 0x0C, 0xF3, 0x0F, 0x10, 0x4F, 0x10, 0xF3, 0x0F, 0x10, 0x47, 0x14};
    const std::byte* found = nullptr;
    for (std::uint32_t i = 0; i <= sections[0].size - pattern.size(); ++i) {
        const auto* code = image.base + sections[0].rva + i;
        if (!matches(code, pattern))
            continue;
        require(!found, "Packet occlusion instruction anchor is ambiguous");
        found = code;
    }
    require(found && image.rva(found) == 0x20CE50, "Unsupported packet occlusion implementation");
    require(image.contains(found, 0x613, 0), "Packet occlusion function exceeds executable text");
    const auto absolute = [&](std::size_t at, std::size_t section, std::uint32_t rva) {
        const auto* value =
            reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(found + at)));
        require(image.contains(value, 4, section) && image.rva(value) == rva,
                "Packet occlusion operand lacks verified module provenance");
        return value;
    };
    require(read<std::uint32_t>(absolute(0x10, 1, 0x4F57A0)) == 0x3F000000,
            "Packet occlusion half constant differs");
    (void)absolute(0x22, 2, 0x5A74C4);
    expect(image, found + 0x251, {0x8B, 0x75, 0x08, 0xF3, 0x0F, 0x10, 0x15, -1, -1, -1, -1});
    const auto call = [&](std::size_t at, std::uint32_t destination) {
        expect(image, found + at, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(image.rva(found)) + at + 5 + read<std::int32_t>(found + at + 1) ==
                    destination,
                "Packet occlusion helper target differs");
    };
    call(0x40B, 0x20CCF0);
    call(0x464, 0x1E0F40);
    call(0x569, 0x1E1320);
    call(0x605, 0x1E0F40);
    expect(image, found + 0x56E,
           {0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3, 0x32, 0xC0, 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    expect(image, found + 0x60A, {0x83, 0xC4, 0x0C, 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    return found;
}
}
void initialize_build_packet_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Packet engine publication requires x86");
    const auto image = checked_image(data.native_cell<void*>(0x43468D20u));
    const auto* entry = acquire(image);
    data.native_cell<std::uintptr_t>(0x4346A7CCu) = reinterpret_cast<std::uintptr_t>(entry);
}
PacketBoxRuntime::PacketBoxRuntime(const NativeDataImage& data)
    : entry_(data.native_cell<std::uintptr_t>(0x4346A7CCu)) {
    require(sizeof(void*) == 4 && entry_,
            "Packet geometry requires its acquired canonical x86 engine entry");
}
network::PacketBoxQuery PacketBoxRuntime::binding() noexcept {
    return {this, invoke};
}
bool PacketBoxRuntime::invoke(void* self, const network::Bounds& moving,
                              const network::Bounds& target, network::Vector& movement,
                              float expansion) {
    return static_cast<PacketBoxRuntime*>(self)->query(moving, target, movement, expansion);
}
bool PacketBoxRuntime::query(const network::Bounds& moving, const network::Bounds& target,
                             network::Vector& movement, float expansion) const {
    require(entry_ != 0, "Packet geometry engine entry is no longer bound");
    return detail::invoke_packet_box(entry_, moving, target, movement, expansion);
}
#if defined(__i386__) && defined(__clang__)
__attribute__((naked, noinline)) bool detail::invoke_packet_box(std::uintptr_t,
                                                                const network::Bounds&,
                                                                const network::Bounds&,
                                                                network::Vector&, float) {
    __asm__ __volatile__("pushl %ebp\n\t"
                         "movl %esp, %ebp\n\t"
                         "movl 8(%ebp), %eax\n\t"
                         "movl 12(%ebp), %ecx\n\t"
                         "movl 16(%ebp), %edx\n\t"
                         "movss 24(%ebp), %xmm3\n\t"
                         "pushl 20(%ebp)\n\t"
                         "call *%eax\n\t"
                         "addl $4, %esp\n\t"
                         "movzbl %al, %eax\n\t"
                         "leave\n\t"
                         "ret");
}
#else
bool detail::invoke_packet_box(std::uintptr_t, const network::Bounds&, const network::Bounds&,
                               network::Vector&, float) {
    throw std::logic_error("Packet geometry requires its Windows x86 SSE ABI bridge");
}
#endif
} //namespace skeet::engine
