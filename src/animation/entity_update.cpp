#include "skeet/animation/entity_update.hpp"
#include "skeet/engine/opaque_native_word.hpp"
#include <bit>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
}
void initialize_animation_state(void* state) noexcept {
    std::memset(static_cast<std::byte*>(state) + 0x13C, 0, 20);
}
bool select_animation_angles(const AnimationAngleHistory& history, std::int32_t requested_frame,
                             RageVector& angles) noexcept {
    return select_animation_angles(AnimationAngleHistoryView(history), requested_frame, angles);
}
bool AnimationAngleHistoryView::active() const noexcept {
    std::uint8_t value;
    std::memcpy(&value, active_, sizeof value);
    return value != 0;
}
bool select_animation_angles(AnimationAngleHistoryView history, std::int32_t requested_frame,
                             RageVector& angles) noexcept {
    if (!history.active())
        return false;
    const auto current_frame = history.frame();
    for (const auto& sample : history.samples()) {
        if (current_frame < sample.frame && sample.frame <= requested_frame) {
            std::memcpy(angles.data(), sample.angles.data(), sizeof sample.angles);
            return true;
        }
    }
    return false;
}
void update_animation_state(void* state, const RageVector& angles,
                            const AnimationUpdateTiming& timing,
                            const AnimationUpdateEngine& engine) {
    if (!engine.update_state)
        return;
    // DEC wraps at INT_MIN rather than invoking signed-overflow behavior.
    write(state, 0x70,
          std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(timing.frame) - 1u));
    //4331EE04/4333DB01 can install their own partially unwritten automatic
    // state when the entity has no animation state. Keep that native stack
    // observation opaque; +70 above is already initialized on this path.
    if (engine::read_opaque_native_float(static_cast<const std::byte*>(state) + 0x6C) ==
        timing.current_time)
        write(state, 0x6C, timing.current_time - timing.interval);
    engine.update_state(engine.context, state, 0.0f, angles[1], angles[0], 1);
    timing.clear_reset_byte();
}
void capture_entity_animation(lagcomp::AnimationState& snapshot, const void* entity,
                              const lagcomp::EntityNetvars& offsets) noexcept {
    const auto* layers = static_cast<const std::byte*>(entity) + offsets.animation_layers;
    lagcomp::capture_animation(
        snapshot, read<const void*>(layers, 0), read<std::int32_t>(layers, 0xC),
        reinterpret_cast<const float*>(static_cast<const std::byte*>(entity) +
                                       offsets.pose_parameters));
}
void restore_entity_animation(const lagcomp::AnimationState& snapshot, void* entity,
                              const lagcomp::EntityNetvars& offsets) noexcept {
    const auto* layers = static_cast<std::byte*>(entity) + offsets.animation_layers;
    lagcomp::restore_animation(
        snapshot, read<void*>(layers, 0), read<std::int32_t>(layers, 0xC),
        reinterpret_cast<float*>(static_cast<std::byte*>(entity) + offsets.pose_parameters));
}
} // namespace skeet::animation
