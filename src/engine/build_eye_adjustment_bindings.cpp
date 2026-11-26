#include "skeet/engine/build_eye_adjustment_bindings.hpp"
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
constexpr std::array<Section, 3> sections{{{".text", 0x1000, 0xB38409, 0x60000020},
                                           {".rdata", 0xB3A000, 0x26629C, 0x40000040},
                                           {".data", 0xDA1000, 0x460C3E0, 0xC0000040}}};
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
    require(module != nullptr, "Eye adjustment requires the initialized client module");
    auto* base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported eye-adjustment client DOS/NT layout");
    const auto* nt = base + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
                read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x65272025 &&
                read<std::uint32_t>(nt + 0x50) == 0x5509000 &&
                read<std::uint32_t>(nt + 0x58) == 0xFA635F,
            "Unsupported eye-adjustment client PE32 build");
    std::array<bool, 3> found{};
    for (std::size_t index = 0; index < 7; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto size = read<std::uint32_t>(section + 8), rva = read<std::uint32_t>(section + 12);
        require(rva <= 0x5509000 && size <= 0x5509000 - rva,
                "Eye-adjustment client section exceeds its image");
        for (std::size_t wanted = 0; wanted < sections.size(); ++wanted) {
            const auto& expected = sections[wanted];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[wanted] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Unsupported eye-adjustment section fingerprint");
            found[wanted] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Eye-adjustment client section is missing");
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
            "Eye-adjustment instruction fingerprint differs");
}
const std::byte* acquire(const Image& image) {
    constexpr std::array pattern{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x08, 0xF3, 0x0F,
                                 0x11, 0x54, 0x24, 0x04, 0x85, 0xD2, 0x0F, 0x88, 0x0F, 0x01, 0,
                                 0,    0x8B, 0x41, 0x04, 0x85, 0xC0, 0x75, 0x12, 0x8B, 0x01, 0x85,
                                 0xC0, 0x74, 0x08, 0x8B, 0x80, 0x2C, 0x01, 0,    0,    0xEB, 0x07,
                                 0x33, 0xC0, 0xEB, 0x03, 0x8B, 0x40, 0x50, 0x3B, 0xD0, 0x0F, 0x8D,
                                 0xEB, 0,    0,    0,    0x52, 0xE8, -1,   -1,   -1,   -1,   0xF3,
                                 0x0F, 0x10, 0x6C, 0x24, 0x04, 0x8B, 0xC8, 0x0F, 0x57, 0xF6};
    const std::byte* found = nullptr;
    for (std::uint32_t i = 0; i <= sections[0].size - pattern.size(); ++i) {
        const auto* code = image.base + sections[0].rva + i;
        if (!matches(code, pattern))
            continue;
        require(found == nullptr, "Eye-adjustment pose implementation is ambiguous");
        found = code;
    }
    require(found && image.rva(found) == 0x7DE760,
            "Eye-adjustment pose implementation is missing or unsupported");
    const auto call_rva = std::int64_t(image.rva(found)) + 0x41 + read<std::int32_t>(found + 0x3D);
    require(call_rva == 0x36B690, "Eye-adjustment descriptor helper call differs");
    const auto* descriptor = image.base + call_rva;
    expect(image, descriptor,
           {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x57, 0x8B, 0xF9, 0x8B, 0x4F, 0x04, 0x85,
            0xC9, 0x75, 0x15, 0x8B, 0x17, 0x8D, 0x0C, 0x80, 0x5F, 0x8B, 0x82, 0x30, 0x01,
            0,    0,    0x8D, 0x04, 0x88, 0x03, 0xC2, 0x5D, 0xC2, 0x04, 0});
    const auto constant = [&](std::size_t offset, std::uint32_t value) {
        const auto* address =
            reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(found + offset)));
        require(image.contains(address, 4, 1) && read<std::uint32_t>(address) == value,
                "Eye-adjustment arithmetic constant lacks verified module provenance");
    };
    constant(0x6B, 0x80000000u);
    constant(0x73, 0x4B000000u);
    constant(0x7F, 0x3F000000u);
    constant(0xB2, 0x3F800000u);
    constant(0xD7, 0x3F800000u);
    expect(image, found + 0xD3,
           {0xF3, 0x0F, 0x10, 0x15, -1,   -1,   -1,   -1,   0xF3, 0x0F, 0x10, 0x49, 0x08,
            0xF3, 0x0F, 0x10, 0x41, 0x0C, 0xF3, 0x0F, 0x5C, 0xE9, 0xF3, 0x0F, 0x5C, 0xC1,
            0x8B, 0x45, 0x08, 0xF3, 0x0F, 0x5E, 0xE8, 0x0F, 0x2F, 0xF5, 0xF3, 0x0F, 0x11,
            0x28, 0x76, 0x12, 0xC7, 0,    0,    0,    0,    0,    0xF3, 0x0F, 0x59, 0,
            0xF3, 0x0F, 0x58, 0xC1, 0x8B, 0xE5, 0x5D, 0xC3, 0x0F, 0x2F, 0xEA, 0x76, 0x06,
            0xC7, 0,    0,    0,    0x80, 0x3F, 0xF3, 0x0F, 0x59, 0,    0xF3, 0x0F, 0x58,
            0xC1, 0x8B, 0xE5, 0x5D, 0xC3, 0x8B, 0x45, 0x08, 0x0F, 0x57, 0xC0, 0xC7, 0,
            0,    0,    0,    0,    0x8B, 0xE5, 0x5D, 0xC3});
    return found;
}
}
void initialize_build_eye_adjustment_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Eye-adjustment engine publication requires x86");
    const auto image = checked_image(data.native_cell<void*>(0x4346A55Cu));
    const auto* entry = acquire(image);
    data.native_cell<std::uintptr_t>(0x43467CF0u) = reinterpret_cast<std::uintptr_t>(entry);
}
void initialize_build_pose_range_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Pose-range publication requires x86");
    const auto image = checked_image(data.native_cell<void*>(0x4346A55Cu));
    const auto* setter = acquire(image);
    const auto* entry = setter + 0x140;
    expect(image, entry,
           {0x55, 0x8B, 0xEC, 0x51, 0xF3, 0x0F, 0x11, 0x55, 0xFC, 0x85, 0xD2, 0x78, 0x3E, 0x8B,
            0x41, 0x04, 0x85, 0xC0, 0x75, 0x12, 0x8B, 0x01, 0x85, 0xC0, 0x74, 0x08, 0x8B, 0x80,
            0x2C, 0x01, 0,    0,    0xEB, 0x07, 0x33, 0xC0, 0xEB, 0x03, 0x8B, 0x40, 0x50, 0x3B,
            0xD0, 0x7D, 0x1E, 0x52, 0xE8, -1,   -1,   -1,   -1,   0xF3, 0x0F, 0x10, 0x40, 0x0C,
            0xF3, 0x0F, 0x5C, 0x40, 0x08, 0xF3, 0x0F, 0x59, 0x45, 0xFC, 0xF3, 0x0F, 0x58, 0x40,
            0x08, 0x8B, 0xE5, 0x5D, 0xC3, 0x0F, 0x57, 0xC0, 0x8B, 0xE5, 0x5D, 0xC3});
    const auto helper = std::int64_t(image.rva(entry)) + 0x33 + read<std::int32_t>(entry + 0x2F);
    require(helper == 0x36B690, "Pose-range descriptor helper differs from its verified setter");
    data.native_cell<std::uintptr_t>(0x43468CA4u) = reinterpret_cast<std::uintptr_t>(entry);
}
PoseRangeRuntime::PoseRangeRuntime(const NativeDataImage& data)
    : entry_(data.native_cell<std::uintptr_t>(0x43468CA4u)) {
    require(sizeof(void*) == 4 && entry_ != 0,
            "Pose-range conversion requires its acquired canonical x86 entry");
}
float PoseRangeRuntime::convert(const void* wrapper, std::uint32_t index, float normalized) const {
    require(entry_ != 0, "Pose-range conversion engine entry is no longer bound");
    return detail::invoke_pose_range(entry_, wrapper, index, normalized);
}
EyePoseParameterRuntime::EyePoseParameterRuntime(const NativeDataImage& data)
    : entry_(data.native_cell<std::uintptr_t>(0x43467CF0u)) {
    require(sizeof(void*) == 4 && entry_ != 0,
            "Eye pose conversion requires its acquired canonical x86 engine entry");
}
float EyePoseParameterRuntime::convert(const void* wrapper, std::uint32_t index, float angle,
                                       float& normalized) const {
    require(entry_ != 0, "Eye pose conversion engine entry is no longer bound");
    return detail::invoke_eye_pose_parameter(entry_, wrapper, index, angle, normalized);
}
#if defined(__i386__) && defined(__clang__)
__attribute__((naked, noinline)) float detail::invoke_pose_range(std::uintptr_t, const void*,
                                                                 std::uint32_t, float) {
    __asm__ __volatile__("pushl %ebp\n\t"
                         "movl %esp, %ebp\n\t"
                         "movl 8(%ebp), %eax\n\t"
                         "movl 12(%ebp), %ecx\n\t"
                         "movl 16(%ebp), %edx\n\t"
                         "movss 20(%ebp), %xmm2\n\t"
                         "call *%eax\n\t"
                         "subl $4, %esp\n\t"
                         "movss %xmm0, (%esp)\n\t"
                         "flds (%esp)\n\t"
                         "leave\n\t"
                         "ret");
}
__attribute__((naked, noinline)) float
detail::invoke_eye_pose_parameter(std::uintptr_t, const void*, std::uint32_t, float, float&) {
    __asm__ __volatile__("pushl %ebp\n\t"
                         "movl %esp, %ebp\n\t"
                         "movl 8(%ebp), %eax\n\t"
                         "movl 12(%ebp), %ecx\n\t"
                         "movl 16(%ebp), %edx\n\t"
                         "movss 20(%ebp), %xmm2\n\t"
                         "pushl 24(%ebp)\n\t"
                         "call *%eax\n\t"
                         "addl $4, %esp\n\t"
                         "subl $4, %esp\n\t"
                         "movss %xmm0, (%esp)\n\t"
                         "flds (%esp)\n\t"
                         "leave\n\t"
                         "ret");
}
#else
float detail::invoke_pose_range(std::uintptr_t, const void*, std::uint32_t, float) {
    throw std::logic_error(
        "Pose-range conversion requires the explicit Windows x86 SSE ABI bridge");
}
float detail::invoke_eye_pose_parameter(std::uintptr_t, const void*, std::uint32_t, float, float&) {
    throw std::logic_error("Eye pose conversion requires the explicit Windows x86 SSE ABI bridge");
}
#endif
} //namespace skeet::engine
