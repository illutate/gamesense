#include "skeet/rage/feedback_entity_owner.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
const void* convar_parent(std::uint32_t encrypted, std::uint32_t mask) noexcept {
    auto* object = reinterpret_cast<const void*>(std::uintptr_t(encrypted ^ mask));
    for (;;) {
        auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            return object;
        object = parent;
    }
}
float convar_float(std::uint32_t encrypted, std::uint32_t mask) noexcept {
    const auto* object = convar_parent(encrypted, mask);
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
}
bool FeedbackEntityBindings::enabled(const ShotFeedbackState&) const noexcept {
    return recording_enabled != 0 ||
           (hitlogs != nullptr && hitlogs->enabled.value != 0 && (hitlogs->events.value & 3) != 0);
}
float FeedbackEntityBindings::now() const noexcept {
    return *current_time;
}
float FeedbackEntityBindings::interval() const noexcept {
    return *tick_interval;
}
float FeedbackEntityBindings::receive_interval() const noexcept {
    return field<float>(receive_clock, 0x20);
}
float FeedbackEntityBindings::lerp() const noexcept {
    const auto update_rate = convar_float(update_rate_convar, 0x03B4819CU);
    const auto interpolation = convar_float(interpolation_convar, 0x7D9DC86FU);
    const auto ratio = convar_float(interpolation_ratio_convar, 0xBE64D223U);
    return maximum_number(interpolation, ratio / update_rate);
}
float FeedbackEntityBindings::correction_minimum() const noexcept {
    return correction_floor;
}
float FeedbackEntityBindings::correction_extra() const noexcept {
    return correction_addend;
}
std::int32_t FeedbackEntityBindings::maximum_unlag_milliseconds() const noexcept {
    return maximum_unlag;
}
bool FeedbackEntityBindings::ray_tests_disabled() const noexcept {
    const auto* object = convar_parent(disable_ray_tests_convar, 0xFCD2DA7AU);
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
void reset_feedback_events(FeedbackEntityOwner& owner) noexcept {
    owner.feedback.recent_position_count = 0;
    owner.feedback.events.feedback.clear();
}
void reconcile_feedback_events(FeedbackEntityOwner& owner) {
    reconcile_shot_feedback(owner.feedback, owner.runtime, owner.bindings);
}
} //namespace skeet::rage
