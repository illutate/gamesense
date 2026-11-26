#include "skeet/anti_aim/jitter.hpp"
#include <bit>
#include <immintrin.h>

namespace skeet::anti_aim {
namespace {
float scalar(std::int32_t value) noexcept {
    return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), value));
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
}
void apply_yaw_jitter(float& yaw, std::int32_t yaw_mode, const JitterSettings& settings,
                      std::uint32_t phase, std::int32_t command_number,
                      const JitterRandom& random) {
    if (yaw_mode == 0)
        return;
    float delta = 0.0f;
    switch (settings.mode) {
    case 1:
        if (phase & 1u)
            delta = scalar(settings.offset);
        break;
    case 2:
        delta = scalar(settings.offset) * 0.5f;
        if (!(phase & 1u))
            delta = negate(delta);
        break;
    case 3: {
        const auto half = absolute(scalar(settings.offset)) * 0.5f;
        random.seed(random.context, command_number);
        delta = random.range(random.context, negate(half), half);
        break;
    }
    case 4: {
        auto amplitude = _mm_set_ss(absolute(scalar(settings.offset)));
        amplitude = _mm_max_ss(amplitude, _mm_set_ss(settings.minimum_skitter));
        amplitude = _mm_min_ss(amplitude, _mm_set_ss(settings.maximum_skitter));
        delta = settings.skitter[phase % 12u] * _mm_cvtss_f32(amplitude);
        break;
    }
    default:
        break;
    }
    yaw += delta;
}
} // namespace skeet::anti_aim
