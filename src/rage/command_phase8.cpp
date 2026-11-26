#include "skeet/rage/command_phase8.hpp"
#include "skeet/rage/preamble.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
void capture_rage_command_frame(PreambleState& state, const client::CommandFrame& frame) noexcept {
    if (state.frame_count != 16)
        state.frame_count = std::bit_cast<std::int32_t>(std::uint32_t(state.frame_count) + 1U);
    state.frame_first = (state.frame_first - 1U) & 15U;
    std::memcpy(&state.frames[state.frame_first], &frame, sizeof frame);
}
} // namespace skeet::rage
