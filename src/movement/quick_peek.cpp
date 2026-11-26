#include "skeet/movement/quick_peek.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/movement/speed_control.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "movement_math.hpp"

namespace skeet {
using namespace movement_detail;
namespace {
struct LiveQuickPeek {
    void* player;
    void* weapon;
    client::Command& command;
    const MovementCommandBindings& bindings;
};
constexpr std::uint32_t movement_buttons = 0x61a;
float length(MovementVector v) {
    return std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
}
MovementVector add(MovementVector a, MovementVector b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
MovementVector sub(MovementVector a, MovementVector b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
MovementVector mul(MovementVector a, float b) {
    return {a.x * b, a.y * b, a.z * b};
}
MovementVector unit(MovementVector a) {
    return mul(a, 1.0f / (length(a) + epsilon));
}
bool hit(const MovementCollisionTrace& t) {
    return 1.0f > t.fraction || t.all_solid || t.start_solid;
}
MovementCollisionRay hull(MovementVector start, MovementVector end, MovementVector minimum,
                          MovementVector maximum) {
    return make_movement_hull_ray(start, end, minimum, maximum);
}
MovementCollisionTrace peek_trace(MovementCollisionEngine& engine, MovementCollisionRay ray,
                                  const QuickPeekFrame& f, std::uint8_t* flag,
                                  bool guard_flag = true, const LiveQuickPeek* live = nullptr) {
    MovementCollisionTrace result{};
    result.fraction = 1.0f;
    result.surface_property = -1;
    result.hitbox = -1;
    const auto mask = live ? movement_command_trace_mask(live->player) : f.trace_mask;
    MovementTraceFlagGuard guard(
        guard_flag ? (live ? live->bindings.environment.engine_trace_flag : flag) : nullptr);
    engine.trace_ray(ray, mask, {MovementCollisionFilterKind::skip_player, f.player_entity, 8, 0},
                     result);
    return result;
}
const QuickPeekShotRecord* record(const QuickPeekFrame& f, std::int32_t command) {
    for (const auto& item : f.shot_records)
        if (item.command_number == command)
            return &item;
    return nullptr;
}
bool shot_triggered(const QuickPeekState& s, const QuickPeekFrame& f) {
    int previous = -1;
    for (int i = 0; i < 16; ++i) {
        const auto* r = record(f, f.last_acknowledged_command - i);
        if (!r)
            break;
        if ((r->flags & 1) && r->shot_time != 0.0f && r->fired) {
            previous = f.last_acknowledged_command - i;
            break;
        }
    }
    if (previous != -1) {
        for (int i = 1, c = previous + 1; i < 16 && c <= f.last_acknowledged_command; ++i, ++c) {
            const auto* r = record(f, c);
            if (!r)
                break;
            if ((r->flags & 1) && r->shot_time == 0.0f && !r->fired)
                return true;
        }
    }
    return f.shot_counter > s.shot_counter;
}
float retreat_heading(QuickPeekState& s, const QuickPeekFrame& f,
                      MovementCollisionEngine& collision, std::uint8_t* flag,
                      const LiveQuickPeek* live = nullptr,
                      const MovementVector* retained_origin = nullptr) {
    MovementVector delta = sub(retained_origin ? *retained_origin : s.origin.value, f.position);
    delta.z = 0;
    const auto direction = unit(delta);
    float yaw = planar_movement_yaw(direction);
    const float distance = length(delta);
    const auto check = add(f.position, mul(direction, minimum_number(100.0f, distance)));
    auto ray = hull({f.position.x, f.position.y, f.position.z + 20},
                    {check.x, check.y, f.position.z + 20}, {-16, -16, 0}, {16, 16, 48});
    const auto first = peek_trace(collision, ray, f, flag, true, live);
    if (!hit(first))
        return yaw;
    MovementVector perpendicular;
    if (first.plane_normal.x == 0 && first.plane_normal.y == 0)
        perpendicular = {0, -1, 0};
    else
        perpendicular = unit({first.plane_normal.y, -first.plane_normal.x, 0});
    for (int i = 0; i < 16; ++i) {
        auto offset = mul(perpendicular, 4.0f * static_cast<float>(i + 1));
        for (int side = 0; side < 2; ++side) {
            const MovementVector origin{f.position.x + offset.x, f.position.y + offset.y,
                                        f.position.z + 20 + offset.z};
            MovementVector toward{s.origin.value.x - origin.x, s.origin.value.y - origin.y, 0};
            const float distance2 = length(toward);
            if (distance2 > 100.0f)
                toward = mul(toward, 100.0f / distance2);
            const auto tested = peek_trace(
                collision, hull(origin, add(origin, toward), {-16, -16, 0}, {16, 16, 48}), f, flag,
                true, live);
            if (!hit(tested)) {
                const auto alternate = unit(offset);
                const MovementVector blended{
                    (direction.x - alternate.x) * first.fraction + alternate.x,
                    (direction.y - alternate.y) * first.fraction + alternate.y, 0};
                return planar_movement_yaw(blended);
            }
            offset = mul(offset, -1.0f);
        }
    }
    return yaw;
}
MovementVector stopping_position(const QuickPeekFrame& f, const UserCommand& command,
                                 const LiveQuickPeek* live = nullptr) {
    MovementVector position = f.position, velocity = f.velocity;
    float speed = length(velocity);
    if (!(speed > 1.0f))
        return position;
    const auto copied_command = command;
    const auto& acceleration_command = live ? copied_command : command;
    const auto speed_physics =
        live ? capture_movement_command_speed(live->player, live->bindings) : f.physics;
    const float maximum =
        minimum_number(speed_physics.player_speed_limit, speed_physics.player_max_speed);
    for (int i = 0; i < 15; ++i) {
        const auto friction =
            live ? capture_movement_command_friction(live->bindings)
                 : MovementCommandFriction{f.physics.stop_speed, f.physics.friction,
                                           f.physics.interval_per_tick};
        const float decreased = maximum_number(
            speed - (maximum_number(friction.stop_speed, speed) * friction.friction) *
                        friction.interval,
            0.0f);
        if (0.1f > decreased)
            break;
        auto opposite = unit(mul(velocity, -1.0f));
        velocity = mul(velocity, decreased / speed);
        if (opposite.z != 0.0f)
            opposite = normalize_horizontal(opposite);
        auto capacity =
            live ? movement_command_ground_acceleration(live->player, acceleration_command,
                                                        velocity, opposite, maximum, live->bindings)
                 : ground_acceleration(f.physics, command, velocity, opposite, maximum);
        if (capacity.amount > decreased && capacity.acceleration != 0.0f)
            capacity = live ? movement_command_ground_acceleration(live->player,
                                                                   acceleration_command, velocity,
                                                                   opposite, 0.01f, live->bindings)
                            : ground_acceleration(f.physics, command, velocity, opposite, 0.01f);
        if (capacity.amount > 0)
            velocity = add(velocity, mul(opposite, capacity.amount));
        position = add(position, mul(velocity, live ? *live->bindings.environment.interval
                                                    : f.physics.interval_per_tick));
        speed = length(velocity);
        if (1.0f > speed)
            break;
    }
    return position;
}
}

void initialize_quick_peek_state(QuickPeekState& s) noexcept {
    s.phase = 0;
    s.saved_buttons = 0;
    s.origin.value.x =
        std::bit_cast<float>(std::bit_cast<std::uint32_t>(s.origin.value.x) & 0xffffff00u);
    s.origin.valid = false;
    s.shot_counter = 0;
}
void reset_quick_peek_state(QuickPeekState& s) noexcept {
    s.phase = 0;
    s.origin.valid = false;
    s.stop_time = 0;
    s.shot_counter = 0;
}
void update_quick_peek_shot_counter(QuickPeekState& s, std::int32_t counter) noexcept {
    s.shot_counter = counter;
}

namespace {
QuickPeekContinuation process_peek(const QuickPeekConfig& config, InputBitmap input,
                                   QuickPeekState& s, QuickPeekFrame f, UserCommand& command,
                                   MovementAngleView angles, std::uint32_t& flags,
                                   std::span<std::uint32_t, 150> marked,
                                   MovementCollisionEngine& collision, std::uint8_t* trace_flag,
                                   const LiveQuickPeek* live) {
    s.distance = config.distance.value == 201 ? 1000.0f : static_cast<float>(config.distance.value);
    s.saved_buttons = f.original_buttons;
    auto& history = s.history[static_cast<std::uint32_t>(f.command_number) % 32];
    if (live) {
        history.command_number = f.command_number;
        const auto captured =
            capture_quick_peek_history(live->player, f.command_number, live->bindings);
        history.position = captured.position;
        history.unavailable = captured.unavailable;
        history.grounded = captured.grounded;
    } else
        history = {f.command_number, f.position, f.history_unavailable,
                   (f.physics.player_flags & 1) != 0};
    if (live && s.stop_time > 0)
        f.physics.interval_per_tick = *live->bindings.environment.interval;
    if (s.stop_time > 0)
        s.stop_time = maximum_number(s.stop_time - f.physics.interval_per_tick, 0.0f);
    if (!config.enabled.value || !hotkey_active(config.hotkey.value.binding, input)) {
        reset_quick_peek_state(s);
        return QuickPeekContinuation::movement_features;
    }
    if (s.stop_time > 0) {
        if (live)
            f.physics.player_flags = movement_command_player_flags(live->player, live->bindings);
        if ((f.physics.player_flags & 1) &&
            !((s.retreat_buttons ^ s.saved_buttons) & movement_buttons)) {
            const auto velocity =
                live ? movement_command_velocity(live->player, live->bindings) : f.velocity;
            const bool wrote = live ? write_live_movement_counter(live->player, command, velocity,
                                                                  angles, live->bindings)
                                    : write_counter_movement(f.physics, command, velocity, angles);
            if (wrote)
                flags |= 1;
            return QuickPeekContinuation::complete;
        }
        s.stop_time = 0;
    }
    if (s.phase == 0) {
        if (!s.origin.valid) {
            auto position = live ? movement_command_position(live->player) : f.position;
            const MovementVector* selected_position = nullptr;
            if (live) {
                f.physics.player_flags =
                    movement_command_player_flags(live->player, live->bindings);
                f.command_number = live->command.number;
            }
            if (f.physics.player_flags & 1) {
                if (live)
                    f.physics.interval_per_tick = *live->bindings.environment.interval;
                const int ticks = static_cast<int>(0.2f / f.physics.interval_per_tick + 0.5f);
                bool grounded = true;
                for (int i = 1; i <= ticks; ++i) {
                    const int number = f.command_number - i;
                    const auto& h = s.history[static_cast<std::uint32_t>(number) % 32];
                    if (h.command_number == number && !h.grounded) {
                        grounded = false;
                        break;
                    }
                }
                const int number = f.command_number - ticks;
                const auto& h = s.history[static_cast<std::uint32_t>(number) % 32];
                if (grounded && h.command_number == number && !h.unavailable) {
                    position = h.position;
                    selected_position = &h.position;
                }
            }
            const auto end = MovementVector{position.x, position.y, position.z - 200.0f};
            if (live) {
                const auto bounds = capture_quick_peek_origin_hull(live->player);
                f.hull_minimum = bounds.minimum;
                f.hull_maximum = bounds.maximum;
                f.trace_mask = bounds.mask;
                if (selected_position)
                    position = *selected_position;
            }
            auto trace = MovementCollisionTrace{};
            trace.fraction = 1;
            trace.surface_property = -1;
            trace.hitbox = -1;
            trace_movement_world(collision, hull(position, end, f.hull_minimum, f.hull_maximum),
                                 f.trace_mask, trace,
                                 live ? live->bindings.environment.engine_trace_flag : trace_flag);
            if (hit(trace))
                s.origin = {trace.end, true};
        }
        if (s.origin.valid) {
            s.start_time = live ? live->bindings.current_time : f.current_time;
            s.phase = 1;
        }
    }
    if (s.phase == 1 && s.saved_buttons & movement_buttons)
        s.phase = 2;
    if (s.phase == 2) {
        flags |= 0x20000;
        if (live) {
            f.command_number = live->command.number;
            f.mark_command = hotkey_active(live->bindings.command_mark_hotkey, input);
        }
        if (f.mark_command)
            marked[static_cast<std::uint32_t>(f.command_number) % 150] = f.command_number;
        s.shot = (config.mode.value & 1) &&
                 (live ? movement_command_quick_peek_shot(s, live->player, live->bindings)
                       : shot_triggered(s, f));
        if (((config.mode.value & 2) && !(s.saved_buttons & movement_buttons)) || s.shot) {
            s.phase = 3;
            s.retreat_start = live ? movement_command_position(live->player) : f.position;
            s.released = false;
            s.retreat_buttons = s.saved_buttons;
            s.start_time = live ? live->bindings.current_time : f.current_time;
        } else {
            if (live && s.origin.valid)
                f.physics.player_flags =
                    movement_command_player_flags(live->player, live->bindings);
            if (s.origin.valid && (f.physics.player_flags & 1)) {
                const auto first_position =
                    live ? movement_command_position(live->player) : f.position;
                const auto delta = sub(s.origin.value, first_position);
                const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                if (distance > s.distance) {
                    const auto second_position =
                        live ? movement_command_position(live->player) : f.position;
                    const auto center =
                        add(second_position, mul(delta, s.distance / (distance + epsilon)));
                    if (live) {
                        const auto bounds = capture_quick_peek_limit_hull(live->player);
                        f.hull_minimum = bounds.minimum;
                        f.hull_maximum = bounds.maximum;
                        f.trace_mask = bounds.mask;
                    }
                    const auto t = peek_trace(collision,
                                              hull({center.x, center.y, center.z + 18},
                                                   {center.x, center.y, center.z - 128},
                                                   f.hull_minimum, f.hull_maximum),
                                              f, trace_flag, false);
                    if (!t.all_solid && !t.start_solid)
                        s.origin = {t.end, true};
                }
            }
        }
    }
    if (s.phase == 3) {
        flags |= 0x20000;
        if (live) {
            f.command_number = live->command.number;
            f.mark_command = hotkey_active(live->bindings.command_mark_hotkey, input);
        }
        if (f.mark_command)
            marked[static_cast<std::uint32_t>(f.command_number) % 150] = f.command_number;
        if (!(s.saved_buttons & movement_buttons))
            s.released = true;
        if (s.released) {
            if (live && s.shot) {
                f.weapon_next_attack = movement_command_weapon_attack(live->weapon, live->bindings);
                f.player_next_attack = movement_command_player_attack(live->player, live->bindings);
                f.current_time = live->bindings.current_time;
            }
            const bool may_cancel =
                ((config.mode.value & 2) && !s.shot) ||
                (s.shot &&
                 f.current_time >= maximum_number(f.player_next_attack, f.weapon_next_attack));
            if (may_cancel && (s.saved_buttons & movement_buttons)) {
                s.phase = 2;
                flags |= 0x8000;
                return QuickPeekContinuation::movement_features;
            }
        }
        const auto retained_origin = s.origin.value;
        if (live)
            f.position = movement_command_position(live->player);
        const float heading =
            retreat_heading(s, f, collision, trace_flag, live, live ? &retained_origin : nullptr);
        if (live)
            f.physics.player_flags = movement_command_player_flags(live->player, live->bindings);
        if (!(f.physics.player_flags & 1)) {
            angles.yaw = heading;
            return QuickPeekContinuation::movement_features;
        }
        if (live) {
            f.position = movement_command_position(live->player);
            f.velocity = movement_command_velocity(live->player, live->bindings);
        }
        const auto predicted = stopping_position(f, command, live);
        auto toward = sub(predicted, s.origin.value);
        toward.z = 0;
        const float distance = length(toward);
        toward = unit(toward);
        auto start = sub(s.retreat_start, s.origin.value);
        start.z = 0;
        start = unit(start);
        if (distance > 0.01f && start.x * toward.x + start.y * toward.y >= 0.0f) {
            angles.yaw = heading;
            command.buttons &= ~speed_button;
            command.forward_move = 450;
            command.side_move = 0;
            flags |= 0x8000;
            return QuickPeekContinuation::complete;
        }
        s.phase = 0;
        if (live) {
            f.current_time = live->bindings.current_time;
            f.physics.interval_per_tick = *live->bindings.environment.interval;
            f.weapon_next_attack = movement_command_weapon_attack(live->weapon, live->bindings);
            f.player_next_attack = movement_command_player_attack(live->player, live->bindings);
        }
        const float elapsed =
            sse_min(sse_max(f.current_time - s.start_time, f.physics.interval_per_tick), 2.0f);
        const float next = sse_min(
            sse_max(maximum_number(f.player_next_attack, f.weapon_next_attack) - f.current_time,
                    0.0f),
            2.0f);
        s.stop_time = sse_min(sse_max(next - elapsed, 0.0f), 0.75f) + 0.2f;
        flags |= 0x200000;
    }
    if (s.phase)
        flags |= 0x8000;
    return QuickPeekContinuation::movement_features;
}
}

QuickPeekContinuation process_quick_peek(const QuickPeekConfig& config, InputBitmap input,
                                         QuickPeekState& state, const QuickPeekFrame& frame,
                                         UserCommand& command, MovementAngleView angles,
                                         std::uint32_t& flags, std::span<std::uint32_t, 150> marked,
                                         MovementCollisionEngine& collision,
                                         std::uint8_t* trace_flag) {
    return process_peek(config, input, state, frame, command, angles, flags, marked, collision,
                        trace_flag, nullptr);
}
QuickPeekContinuation process_live_quick_peek(const QuickPeekConfig& config, InputBitmap input,
                                              QuickPeekState& state, void* player, void* weapon,
                                              client::Command& command,
                                              std::uint32_t original_buttons,
                                              MovementAngleView angles, std::uint32_t& flags,
                                              const MovementCommandBindings& bindings) {
    QuickPeekFrame frame{};
    frame.original_buttons = original_buttons;
    frame.command_number = command.number;
    frame.player_entity = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(player));
    const LiveQuickPeek live{player, weapon, command, bindings};
    return process_peek(config, input, state, frame, command.movement, angles, flags,
                        bindings.marked_commands, bindings.collision,
                        bindings.environment.engine_trace_flag, &live);
}
} // namespace skeet
