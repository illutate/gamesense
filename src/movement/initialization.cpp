#include "skeet/movement/initialization.hpp"
#include <limits>

namespace skeet {

void initialize_movement_state(MovementCommandOwner& state, ConfigRegistries& registry,
                               const void* air_accelerate, const void* jump_speed_cap) {
    state.air_accelerate = air_accelerate;
    state.jump_speed_cap = jump_speed_cap;
    initialize_movement_config(registry, state.config);
    state.previous_buttons_1e0 = 0;
    state.suppressed_buttons_1e4 = 0;
    state.feedback.primary_heading = (std::numeric_limits<float>::max)();
    state.feedback.secondary_heading = (std::numeric_limits<float>::max)();
    state.limits = {845.5f, 90.0f};
    state.side_magnitude = 450.0f;
    state.brake_reduction_limit = 15.0f;
    state.brake_threshold = 30.0f;
    state.angle_step = 60.0f;
    state.field_1d8 = 286.0f;
    initialize_quick_peek_config(registry, state.quick_peek_config);
    initialize_quick_peek_state(state.quick_peek);

    auto& duck = state.duck_peek;
    duck.target = 0.0f;
    duck.field_530 = -1;
    duck.phase = 0;
    duck.choked = 0;
    duck.original_buttons = 0;
    duck.previous_buttons = 0;
    duck.last_key = 27;
    duck.crouch_active = false;
    duck.release_latch = false;
    duck.configured_limit = nullptr;
}

void finish_movement_state_initialization(MovementCommandOwner& state) noexcept {
    state.callback_flags_1dc = 0;
    state.feedback.turn_direction = 0.0f;
    state.feedback.secondary_heading = 0.0f;
    state.feedback.was_grounded = 1;
    state.feedback.prediction_blocked = 0;
}

void reset_movement_state(MovementCommandOwner& state) noexcept {
    state.feedback.turn_direction = 0.0f;
    state.feedback.primary_heading = (std::numeric_limits<float>::max)();
    state.feedback.secondary_heading = (std::numeric_limits<float>::max)();
    state.callback_flags_1dc = 0;
    state.feedback.was_grounded = 1;
    state.previous_buttons_1e0 = 0;
    state.suppressed_buttons_1e4 = 0;
    reset_quick_peek_state(state.quick_peek);
    state.duck_peek.target = 0.0f;
    state.duck_peek.phase = 0;
    state.duck_peek.crouch_active = false;
    state.duck_peek.last_key = 27;
}

void bind_movement_settings(MovementCommandOwner& state, ConfigRegistries& registry) {
    state.duck_peek.configured_limit =
        &static_cast<IntegerConfig*>(find_config(registry.descriptors, 0xF17FEF11u))->value;
}

} //namespace skeet
