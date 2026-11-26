#include "skeet/client/player_physics_hooks.hpp"
#include "player_physics_hooks_detail.hpp"
#include <bit>
#include <immintrin.h>

namespace skeet::client {
void PlayerPhysicsHooks::eye_position(void* player, std::array<float, 3>& output) {
    using namespace player_physics_detail;
    // Complete43367C8B and43382222, preserving the virtual-origin read and
    // the SSE component return's calculation order before publishing output.
    const auto* origin = engine::invoke<const std::uint32_t*>(player, 10);
    std::array<std::uint32_t, 3> captured;
    captured[0] = origin[0];
    captured[1] = origin[1];
    auto duck = _mm_load_ss(reinterpret_cast<const float*>(static_cast<const std::byte*>(player) +
                                                           bindings.fields.duck_amount_3309c0cf));
    duck = _mm_mul_ss(duck, duck);
    captured[2] = origin[2];
    auto smooth = duck;
    duck = _mm_add_ss(duck, duck);
    smooth = _mm_mul_ss(smooth, _mm_set_ss(3.0f));
    duck = _mm_mul_ss(
        duck, _mm_load_ss(reinterpret_cast<const float*>(static_cast<const std::byte*>(player) +
                                                         bindings.fields.duck_amount_3309c0cf)));
    smooth = _mm_sub_ss(smooth, duck);
    smooth = _mm_sub_ss(_mm_set_ss(1.0f), smooth);
    smooth = _mm_mul_ss(smooth, _mm_set_ss(18.0f));
    smooth = _mm_add_ss(smooth, _mm_set_ss(46.0f));
    const auto z = _mm_castsi128_ps(_mm_cvtsi32_si128(std::bit_cast<std::int32_t>(captured[2])));
    smooth = _mm_add_ss(smooth, z);
    store(output.data(), 0, captured[0]);
    store(output.data(), 4, captured[1]);
    store(output.data(), 8, std::uint32_t(_mm_cvtsi128_si32(_mm_castps_si128(smooth))));
}
} //namespace skeet::client
