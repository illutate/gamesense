#include "skeet/lagcomp/native_history_boundaries.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include <stdexcept>
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
void* named(const char* name) {
    const auto module = GetModuleHandleW(L"msvcrt.dll");
    if (!module)
        throw std::runtime_error("History math requires the loaded msvcrt.dll");
    auto* const result = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!result)
        throw std::runtime_error("Loaded msvcrt.dll lacks a required history import");
    return result;
}
struct Entries {
    void* fmod;
    void* power;
    void* arccosine;
    void* substring;
};
const Entries& entries() {
    static const Entries result{named("_CIfmod"), named("__libm_sse2_powf"),
                                named("__libm_sse2_acosf"), named("strstr")};
    return result;
}
double remainder(double numerator, double denominator) {
    double result;
    __asm__ __volatile__("fldl %1\n\tfldl %2\n\tcall *%3\n\tfstpl %0"
                         : "=m"(result)
                         : "m"(numerator), "m"(denominator), "r"(entries().fmod)
                         : "eax", "ecx", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
                           "xmm6", "xmm7", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                           "st(6)", "st(7)", "memory", "cc");
    return result;
}
float power(float base, float exponent) {
    register __m128 first __asm__("xmm0") = _mm_set_ss(base);
    register __m128 second __asm__("xmm1") = _mm_set_ss(exponent);
    __asm__ __volatile__("call *%2"
                         : "+x"(first), "+x"(second)
                         : "r"(entries().power)
                         : "eax", "ecx", "edx", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                           "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
                           "memory", "cc");
    return _mm_cvtss_f32(first);
}
float arccosine(float value) {
    register __m128 first __asm__("xmm0") = _mm_set_ss(value);
    __asm__ __volatile__("call *%1"
                         : "+x"(first)
                         : "r"(entries().arccosine)
                         : "eax", "ecx", "edx", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
                           "xmm7", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)",
                           "st(7)", "memory", "cc");
    return _mm_cvtss_f32(first);
}
int substring(const char* text, const char* part) {
    using Entry = char*(__cdecl*)(const char*, const char*);
    return reinterpret_cast<Entry>(entries().substring)(text, part) != nullptr;
}
double __vectorcall environment_atan2(double y, double x) {
    return animation::native_sse_atan2(y, x);
}
void displacement(void* entry, void* studio, std::int32_t sequence, const float* poses,
                  Vector& output) {
    const void* arguments[]{poses, &output, entry};
    //433E0681's ADD ESP,8 is required: client+19D600 returns with plain RET.
    __asm__ __volatile__("pushl 4(%2)\n\tpushl (%2)\n\tcall *8(%2)\n\taddl $8,%%esp"
                         : "+c"(studio), "+d"(sequence)
                         : "S"(arguments)
                         : "eax", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                           "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
                           "memory", "cc");
}
void update_animation(void* entry, void* state, float zero, float yaw, float pitch,
                      std::int32_t force) {
    const std::uintptr_t arguments[]{std::uintptr_t(force),
                                     reinterpret_cast<std::uintptr_t>(entry)};
    register __m128 first __asm__("xmm0") = _mm_set_ss(zero);
    register __m128 second __asm__("xmm1") = _mm_set_ss(yaw);
    register __m128 third __asm__("xmm2") = _mm_set_ss(pitch);
    std::uint32_t zero_register = 0;
    __asm__ __volatile__("pushl (%5)\n\tcall *4(%5)"
                         : "+c"(state), "+d"(zero_register), "+x"(first), "+x"(second), "+x"(third)
                         : "S"(arguments)
                         : "eax", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "st", "st(1)", "st(2)",
                           "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory", "cc");
}
}
HistoryNumericBindings native_history_numeric_bindings() {
    (void)entries();
    return {remainder, environment_atan2, animation::native_sse_atan2, power, arccosine, substring};
}
HistoryAnimationMovementEngine native_history_movement_engine(void* entry) {
    return {entry, entry ? displacement : nullptr};
}
animation::AnimationUpdateEngine native_history_animation_engine(void* entry) {
    return {entry, entry ? update_animation : nullptr};
}
} //namespace skeet::lagcomp
#else
namespace skeet::lagcomp {
HistoryNumericBindings native_history_numeric_bindings() {
    throw std::logic_error("Native history boundaries require Windows x86");
}
HistoryAnimationMovementEngine native_history_movement_engine(void*) {
    throw std::logic_error("Native history movement requires Windows x86");
}
animation::AnimationUpdateEngine native_history_animation_engine(void*) {
    throw std::logic_error("Native history animation requires Windows x86");
}
}
#endif
