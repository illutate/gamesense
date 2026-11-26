#include "skeet/client/player_physics_hooks.hpp"
#include "../client/player_physics_hooks_detail.hpp"
#include <bit>
#include <immintrin.h>
#include <stdexcept>

extern "C" {
const std::uint32_t* skeet_player_velocity_return_cell = nullptr;
}

namespace skeet::client {
namespace player_physics_detail {
void bind_velocity_continuation(const std::uint32_t& continuation) noexcept {
    skeet_player_velocity_return_cell = &continuation;
}
}
namespace {
using namespace player_physics_detail;

void clamp_absolute_velocity(std::array<float, 3>& velocity, const void* variable) {
    // Complete433E4973. ConVar parent traversal precedes all vector reads.
    for (;;) {
        const auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    const auto bits = field<std::uint32_t>(variable, 0x2C) ^
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
    const auto maximum = _mm_castsi128_ps(_mm_cvtsi32_si128(std::bit_cast<std::int32_t>(bits)));
    const auto minimum = _mm_xor_ps(
        maximum, _mm_castsi128_ps(_mm_cvtsi32_si128(std::bit_cast<std::int32_t>(0x80000000U))));
    for (std::size_t index = 0; index < 3; ++index) {
        auto* destination = reinterpret_cast<std::byte*>(velocity.data()) + index * 4;
        const auto value = _mm_castsi128_ps(
            _mm_cvtsi32_si128(std::bit_cast<std::int32_t>(field<std::uint32_t>(destination, 0))));
        if (_mm_movemask_ps(_mm_cmpunord_ss(value, value)) & 1)
            store(destination, 0, std::uint32_t(0));
        else if (_mm_comigt_ss(value, maximum))
            store(destination, 0, bits);
        else if (_mm_comigt_ss(minimum, value))
            store(destination, 0, bits ^ 0x80000000U);
    }
}

// Complete43326986 continuation. The caller's stack vector is loaded into
// the same XMM registers before jumping to the live engine continuation.
// The scratch EAX is restored, and ESP has its original value at the jump.
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define SKEET_VELOCITY_STRING_IMPL(value) #value
#define SKEET_VELOCITY_STRING(value) SKEET_VELOCITY_STRING_IMPL(value)
__attribute__((naked, noinline)) void absolute_velocity_return_gate() {
    __asm__ __volatile__("movss 0x28(%esp), %xmm1\n\t"
                         "movss 0x24(%esp), %xmm3\n\t"
                         "movss 0x20(%esp), %xmm4\n\t"
                         "movaps %xmm4, %xmm0\n\t"
                         "movaps %xmm3, %xmm2\n\t"
                         "mulss %xmm4, %xmm0\n\t"
                         "mulss %xmm3, %xmm2\n\t"
                         "addss %xmm0, %xmm2\n\t"
                         "movaps %xmm1, %xmm0\n\t"
                         "mulss %xmm1, %xmm0\n\t"
                         "addss %xmm0, %xmm2\n\t"
                         "pushl %eax\n\t"
                         "movl " SKEET_VELOCITY_STRING(
                             __USER_LABEL_PREFIX__) "skeet_player_velocity_return_cell, %eax\n\t"
                                                    "movl (%eax), %eax\n\t"
                                                    "xchgl %eax, (%esp)\n\t"
                                                    "retl");
}
#undef SKEET_VELOCITY_STRING
#undef SKEET_VELOCITY_STRING_IMPL
#elif defined(_M_IX86) && defined(_MSC_VER)
__declspec(naked) void absolute_velocity_return_gate() {
    __asm {
        movss xmm1,dword ptr [esp+28h]
        movss xmm3,dword ptr [esp+24h]
        movss xmm4,dword ptr [esp+20h]
        movaps xmm0,xmm4
        movaps xmm2,xmm3
        mulss xmm0,xmm4
        mulss xmm2,xmm3
        addss xmm2,xmm0
        movaps xmm0,xmm1
        mulss xmm0,xmm1
        addss xmm2,xmm0
        push eax
        mov eax,skeet_player_velocity_return_cell
        mov eax,dword ptr [eax]
        xchg eax,dword ptr [esp]
        ret
    }
}
#endif
}

void PlayerPhysicsHooks::estimate_absolute_velocity(void* player, std::array<float, 3>& output,
                                                    std::uintptr_t& caller_return) {
    using namespace player_physics_detail;
    if (!bindings.command_mode_43467224 && !bindings.alternate_mode_4346a740) {
        original<void>(player, 145, &output);
        return;
    }
    // The original copies the words sequentially, including output aliases.
    for (std::size_t index = 0; index < 3; ++index)
        store(output.data(), index * 4,
              field<std::uint32_t>(player, bindings.fields.absolute_velocity_36375c23 + index * 4));
    const auto* variable = reinterpret_cast<const void*>(
        std::uintptr_t(bindings.maximum_velocity_convar_43468c2c ^ 0x3FE3AE0CU));
    clamp_absolute_velocity(output, variable);
    if (bindings.velocity_return_site_43467f0c && bindings.velocity_continuation_434757c4 &&
        caller_return == bindings.velocity_return_site_43467f0c) {
#if defined(__i386__) || defined(_M_IX86)
        caller_return = reinterpret_cast<std::uintptr_t>(&absolute_velocity_return_gate);
#else
        throw std::logic_error("Player velocity return continuation requires the x86 hook ABI");
#endif
    }
}
} //namespace skeet::client
