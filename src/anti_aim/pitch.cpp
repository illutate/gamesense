#include "skeet/anti_aim/pitch.hpp"
#include <bit>
#include <immintrin.h>

namespace skeet::anti_aim {
namespace {
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
}
void apply_pitch(float& pitch, const PitchSettings& settings, const PitchFrame& frame,
                 const PitchInterfaces& external) {
    auto mode = settings.mode;
    if (!mode)
        return;
    if ((frame.flags & 0x200000u) && (frame.buttons & 0x20u) && frame.movement_type == 3)
        mode = 1;
    if (mode == 4) {
        external.random_seed(external.context, frame.command_number);
        mode = external.random_integer(external.context, 1, 5);
    }
    switch (mode) {
    case 1:
        pitch = settings.maximum_pitch;
        break;
    case 2:
        if ((frame.flags & 8u) && external.player_code(external.context) == 100)
            pitch = negate(settings.special_angle) - frame.angle_9c;
        else
            pitch = settings.maximum_pitch;
        break;
    case 3:
        if (frame.has_weapon) {
            auto value = _mm_set_ss(frame.weapon_pitch_340);
            value = _mm_max_ss(value, _mm_set_ss(negate(settings.maximum_pitch)));
            value = _mm_min_ss(value, _mm_set_ss(settings.maximum_pitch));
            pitch = _mm_cvtss_f32(value);
            if ((std::bit_cast<std::uint32_t>(pitch) & 0x7F800000u) == 0x7F800000u)
                pitch = settings.maximum_pitch;
        }
        break;
    case 5:
        pitch = (frame.flags & 8u) ? settings.special_angle - frame.angle_9c
                                   : negate(settings.maximum_pitch);
        break;
    case 6:
        pitch = _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), settings.custom_pitch));
        break;
    default:
        break;
    }
}
} // namespace skeet::anti_aim
