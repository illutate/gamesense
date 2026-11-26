#include "skeet/engine/build_hitbox_clip_bindings.hpp"
#include "skeet/engine/trace.hpp"
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const void* pointer, std::size_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(pointer) + offset, sizeof value);
    return value;
}
template <class T> void write(void* pointer, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(pointer) + offset, &value, sizeof value);
}
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::invalid_argument(reason);
}
void expect(const std::byte* code, std::initializer_list<int> bytes) {
    for (const auto value : bytes) {
        require(value < 0 || std::uint8_t(*code) == value,
                "Hitbox clip engine instruction fingerprint differs");
        ++code;
    }
}
int invoke_clip(void* entry, const TraceRay* ray, const void* hitbox, const Matrix3x4* matrix,
                void* trace) {
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    const void* arguments[]{matrix, trace, entry};
    int result;
    __asm__ __volatile__("pushl 4(%3)\n\tpushl (%3)\n\tcall *8(%3)\n\taddl $8,%%esp"
                         : "=a"(result), "+c"(ray), "+d"(hitbox)
                         : "S"(arguments)
                         : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "st",
                           "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory",
                           "cc");
    return result;
#elif defined(_MSC_VER) && defined(_M_IX86)
    int result;
    __asm {push trace push matrix mov ecx,ray mov edx,hitbox mov eax,entry call eax add esp,8 mov result,eax}
    return result;
#else
    (void)entry;
    (void)ray;
    (void)hitbox;
    (void)matrix;
    (void)trace;
    throw std::logic_error("Hitbox clip requires its native x86 register ABI");
#endif
}
int clip(void* context, const RageRay& input, const RageScanHitbox& box, RageTrace& output) {
    auto& data = *static_cast<NativeDataImage*>(context);
    auto* const entry = data.native_cell<void*>(0x43468C3Cu);
    require(entry != nullptr, "Hitbox clip engine entry is not published");
    TraceRay ray;
    for (unsigned axis = 0; axis != 3; ++axis) {
        ray.start[axis] = input.start[axis];
        ray.delta[axis] = input.delta[axis];
    }
    ray.is_ray = 1;
    ray.swept = (input.delta[0] * input.delta[0] + input.delta[1] * input.delta[1]) +
                    input.delta[2] * input.delta[2] !=
                0.0f;
    // Other engine trace fields have no consumed input in this boundary. Keep
    // their scratch bytes untouched while preserving the decoded live fields.
    std::array<std::byte, trace_result_size> trace;
    write(trace.data(), 0x0C, output.point);
    write(trace.data(), 0x18, output.normal);
    write(trace.data(), 0x2C, output.fraction);
    write(trace.data(), 0x37, std::uint8_t(output.start_solid));
    const auto result = invoke_clip(entry, &ray, box.engine_hitbox, &box.transform, trace.data());
    output.point = read<RageVector>(trace.data(), 0x0C);
    output.normal = read<RageVector>(trace.data(), 0x18);
    output.fraction = read<float>(trace.data(), 0x2C);
    output.start_solid = read<std::uint8_t>(trace.data(), 0x37) != 0;
    return result;
}
}
void* initialize_build_hitbox_clip_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Hitbox clip engine producer requires x86 pointers");
    auto* const base = data.native_cell<std::byte*>(0x4346A55Cu);
    require(base != nullptr, "Hitbox clip requires its actual loaded client module");
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base, 0x3C) == 0x150,
            "Unsupported hitbox clip client DOS header");
    const auto* const nt = base + 0x150;
    require(
        read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt, 4) == 0x14C &&
            read<std::uint16_t>(nt, 6) == 7 && read<std::uint16_t>(nt, 0x14) == 0xE0 &&
            read<std::uint16_t>(nt, 0x18) == 0x10B && read<std::uint32_t>(nt, 8) == 0x65272025 &&
            read<std::uint32_t>(nt, 0x50) == 0x5509000 && read<std::uint32_t>(nt, 0x58) == 0xFA635F,
        "Unsupported hitbox clip client build");
    const auto* const text = nt + 0xF8;
    require(!std::memcmp(text, ".text\0\0\0", 8) && read<std::uint32_t>(text, 8) == 0xB38409 &&
                read<std::uint32_t>(text, 12) == 0x1000 &&
                read<std::uint32_t>(text, 36) == 0x60000020,
            "Unsupported hitbox clip client text section");
    auto* const entry = base + 0x7DEB00;
    expect(entry, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0xF3, 0x0F, 0x10, 0x42, 0x30, 0x81, 0xEC,
                   0xB0, 0,    0,    0,    0x0F, 0x57, 0xC9, 0x0F, 0x2F, 0xC1, 0x56, 0x57, 0x8B,
                   0xF9, 0x76, 0x14, 0xFF, 0x75, 0x0C, 0xFF, 0x75, 8,    0xE8, -1,   -1,   -1,
                   -1,   0x83, 0xC4, 8,    0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC3});
    require(std::int64_t(0x7DEB28) + read<std::int32_t>(entry, 0x24) == 0x7DE900,
            "Hitbox clip capsule branch target differs");
    expect(entry + 0x31, {0x8B, 0x45, 0x0C, 0xF3, 0x0F, 0x10, 0x0D, -1,   -1,   -1,   -1,   0x8B,
                          0x75, 8,    0xF3, 0x0F, 0x10, 0x57, 0x10, 0xF3, 0x0F, 0x10, 0x40, 0x2C});
    data.native_cell<void*>(0x43468C3Cu) = entry;
    return entry;
}
RageBoxClipper native_hitbox_clipper(NativeDataImage& data) {
    require(data.native_cell<void*>(0x43468C3Cu) != nullptr,
            "Hitbox clip requires its verified published engine entry");
    return {&data, clip};
}
} //namespace skeet::engine
