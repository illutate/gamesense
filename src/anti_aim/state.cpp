#include "skeet/anti_aim/state.hpp"
#include <bit>

namespace skeet::anti_aim {
void reset_state(State& state) noexcept {
    state.flags_130 |= 1;
    state.value_1ac = 0;
    state.value_1b4 = -1;
    state.value_1c4 = -1;
    state.value_158 = -1;
    state.value_190 = 0;
    state.value_1a8 = std::bit_cast<float>(0x7F7FFFFFu);
    state.value_1a0 = 58.0f;
}
} // namespace skeet::anti_aim
