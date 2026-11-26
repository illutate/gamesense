#include "skeet/anti_aim/runtime.hpp"
#include <cstring>
#include <limits>

namespace skeet::anti_aim {
void initialize_command_state_prefix(AngleStageState& state, PrecalculationState& precalculation,
                                     AngleParameters& parameters) noexcept {
    state.saved_yaw_198 = 0.0f;
    state.value_1b4 = -1;
    state.value_1c4 = -1;
    parameters.value_134 = 60.0f;
    parameters.value_138 = 180.0f;
    parameters.value_13c = 118.0f;
    parameters.value_140 = 58.0f;
    parameters.value_144 = 179.0f;
    parameters.value_148 = 90.0f;
    parameters.value_14c = 20.0f;
    parameters.value_150 = 89.0f;
    parameters.value_154 = 0.75f;
    state.cached_yaw_18c = std::numeric_limits<float>::max();
    state.value_190 = 0;
    precalculation.micro_move_19c = 1.01f;
    state.value_1a8 = std::numeric_limits<float>::max();
    state.value_1ac = 0;
    state.value_1b0 = 0;
}
void initialize_command_animation_state(AngleStageState& state, FinisherState& finisher) noexcept {
    animation::initialize_animation_state(finisher.animation_1d8.data());
    state.flags_130 |= 1u;
    // The logical source view mirrors the same native field as the retained
    // animation buffer; this copy does not initialize or alter the raw field.
    std::memcpy(&state.reference_yaw_258, finisher.animation_1d8.data() + 0x80, sizeof(float));
}
void initialize_command_pattern(AngleParameters& parameters) noexcept {
    parameters.pattern_15c = {-1, 1, 0, -1, 1, -1, 0, 1, -1, 0, 1, 0};
}
void initialize_command_state(AngleStageState& state, PrecalculationState& precalculation,
                              FinisherState& finisher, AngleParameters& parameters) noexcept {
    initialize_command_state_prefix(state, precalculation, parameters);
    initialize_command_animation_state(state, finisher);
    initialize_command_pattern(parameters);
}
} // namespace skeet::anti_aim
