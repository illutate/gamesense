#include "skeet/animation/native_trigonometry.hpp"
#include <stdexcept>
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xmmintrin.h>
#include <emmintrin.h>

namespace skeet::animation {
namespace {
void* named_entry(const char* name) {
    const auto module = GetModuleHandleW(L"msvcrt.dll");
    if (!module)
        throw std::runtime_error("Native SSE math requires the loaded msvcrt.dll");
    auto* const entry = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!entry)
        throw std::runtime_error("Loaded msvcrt.dll lacks a required native SSE math export");
    return entry;
}
struct Entries {
    void* sine;
    void* cosine;
};
const Entries& entries() {
    static const Entries result = [] {
        const auto module = GetModuleHandleW(L"msvcrt.dll");
        if (!module)
            throw std::runtime_error("Native trigonometry requires the loaded msvcrt.dll");
        auto* const sine = reinterpret_cast<void*>(GetProcAddress(module, "__libm_sse2_sinf"));
        auto* const cosine = reinterpret_cast<void*>(GetProcAddress(module, "__libm_sse2_cosf"));
        if (!sine || !cosine)
            throw std::runtime_error("Loaded msvcrt.dll lacks the native SSE trigonometry exports");
        return Entries{sine, cosine};
    }();
    return result;
}
float invoke(void* entry, float value) {
    register __m128 lanes __asm__("xmm0") = _mm_set_ss(value);
    __asm__ __volatile__("call *%1"
                         : "+x"(lanes)
                         : "r"(entry)
                         : "eax", "ecx", "edx", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
                           "xmm7", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)",
                           "st(7)", "memory", "cc");
    return _mm_cvtss_f32(lanes);
}
float sine(float value) {
    return invoke(entries().sine, value);
}
float cosine(float value) {
    return invoke(entries().cosine, value);
}
}
const RageTrigonometry& native_sse_trigonometry() {
    (void)entries();
    static const RageTrigonometry result{sine, cosine};
    return result;
}
double native_sse_atan2(double y, double x) {
    static auto* const entry = named_entry("__libm_sse2_atan2");
    register __m128d first __asm__("xmm0") = _mm_set_sd(y);
    register __m128d second __asm__("xmm1") = _mm_set_sd(x);
    __asm__ __volatile__("call *%2"
                         : "+x"(first), "+x"(second)
                         : "r"(entry)
                         : "eax", "ecx", "edx", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                           "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
                           "memory", "cc");
    return _mm_cvtsd_f64(first);
}
float native_sse_exponential(float value) {
    static auto* const entry = named_entry("__libm_sse2_expf");
    return invoke(entry, value);
}
float native_sse_tangent(float value) {
    static auto* const entry = named_entry("__libm_sse2_tanf");
    return invoke(entry, value);
}
float native_sse_arctangent(float value) {
    static auto* const entry = named_entry("__libm_sse2_atanf");
    return invoke(entry, value);
}
} //namespace skeet::animation
#else
namespace skeet::animation {
const RageTrigonometry& native_sse_trigonometry() {
    throw std::logic_error("Native SSE trigonometry requires Windows x86");
}
double native_sse_atan2(double, double) {
    throw std::logic_error("Native SSE atan2 requires Windows x86");
}
float native_sse_exponential(float) {
    throw std::logic_error("Native SSE exponential requires Windows x86");
}
float native_sse_tangent(float) {
    throw std::logic_error("Native SSE tangent requires Windows x86");
}
float native_sse_arctangent(float) {
    throw std::logic_error("Native SSE arctangent requires Windows x86");
}
}
#endif
