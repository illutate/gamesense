#include "skeet/movement/raw_scout_gate.hpp"
#include "skeet/movement/bunny_hop.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <cmath>

namespace skeet {
namespace {
using namespace movement_detail;

bool obstructed(const MovementCollisionTrace& trace) noexcept {
    // The native COMISS/JA edge also treats an unordered fraction as clear.
    return trace.fraction < 1.0f || trace.all_solid || trace.start_solid;
}

bool clear_scout_path(const RawScoutGateFrame& frame, const RawScoutGateBindings& bindings,
                      MovementCollisionTrace& trace) {
    auto* collideable = engine::invoke<void*>(bindings.player, 3);
    const auto* maximum = engine::invoke<const MovementVector*>(collideable, 2);
    collideable = engine::invoke<void*>(bindings.player, 3);
    const auto* minimum = engine::invoke<const MovementVector*>(collideable, 1);
    MovementAngles view;
    // Complete boundary wrapper 0x4339FB19: engine-client virtual +0x48.
    engine::invoke<void>(bindings.engine_client, 18, &view);

    const float yaw = view.yaw * degrees_to_radians;
    const float sine_yaw = std::sin(yaw);
    const float cosine_yaw = std::cos(yaw);
    const float pitch = view.pitch * degrees_to_radians;
    // The original also evaluates sin(pitch), but discards its return value.
    (void)std::sin(pitch);
    const float cosine_pitch = std::cos(pitch);
    const float x = cosine_pitch * cosine_yaw;
    const float y = cosine_pitch * sine_yaw;
    const float inverse = 1.0f / (std::sqrt((x * x + y * y) + 0.0f * 0.0f) + epsilon);
    const MovementVector position = frame.position;
    const MovementVector end{(inverse * x) * 64.0f + position.x, (inverse * y) * 64.0f + position.y,
                             (inverse * 0.0f) * 64.0f + position.z};
    auto ray = make_movement_hull_ray(position, end, *minimum, *maximum);
    trace_movement_world(bindings.collision, ray, 0x1400bu, trace, bindings.engine_trace_flag);
    if (!obstructed(trace))
        return true;

    // Bounds remain live references across the first actual engine trace.
    const MovementVector raised{position.x, position.y, position.z + 64.0f};
    const MovementVector raised_end{end.x, end.y, end.z + 64.0f};
    ray = make_movement_hull_ray(raised, raised_end, *minimum, *maximum);
    trace_movement_world(bindings.collision, ray, 0x1400bu, trace, bindings.engine_trace_flag);
    return !obstructed(trace);
}
}

RawScoutGateResult process_raw_scout_gate(const RawScoutGateSettings& settings,
                                          const AirStrafeSelection& selection,
                                          const RawScoutGateFrame& frame,
                                          const MovementFeedback& feedback, UserCommand& command,
                                          const RawScoutGateBindings& bindings) {
    const float speed = movement_detail::horizontal_length(frame.velocity);
    const bool grounded = (frame.player_flags & on_ground) != 0;
    if (grounded && (!(command.buttons & jump_button) || !(2.0f > speed))) {
        return !feedback.was_grounded && (command.buttons & jump_button)
                   ? RawScoutGateResult::continue_air
                   : RawScoutGateResult::ground_speed_tail;
    }

    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    const float squared_input =
        command.forward_move * command.forward_move + command.side_move * command.side_move;
    if (frame.has_weapon && frame.weapon_id == 0x28 && 2.0f > speed &&
        !(command.buttons & 0x618u) && 4.0f > squared_input &&
        !clear_scout_path(frame, bindings, trace))
        return RawScoutGateResult::ground_speed_tail;

    if (!grounded)
        return RawScoutGateResult::continue_air;
    recover_bunny_hop_trace(
        settings.bunny_hop_enabled, {trace.fraction, trace.all_solid != 0, trace.start_solid != 0},
        (selection.movement_key_mode & 0xffu) != 0, command, settings.forced_forward_move);
    return RawScoutGateResult::ground_speed_tail;
}

} // namespace skeet
