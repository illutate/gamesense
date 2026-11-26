#include "skeet/movement/blockbot.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <cstring>
#include <limits>

namespace skeet {
namespace {
using namespace movement_detail;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
MovementVector origin(void* player) {
    return *engine::invoke<const MovementVector*>(player, 10);
}
MovementVector center(void* player) { // complete433FB98A
    auto result = origin(player);
    const auto* minimum = engine::invoke<const MovementVector*>(player, 76);
    const auto* maximum = engine::invoke<const MovementVector*>(player, 77);
    result.z += (maximum->z + minimum->z) * 0.5f;
    return result;
}
float target_score(void* player, std::int32_t team, MovementVector eye, MovementVector forward,
                   const BlockbotOffsets& offsets) { // complete433EA084
    constexpr auto rejected = std::numeric_limits<float>::max();
    if (!player || !engine::invoke<bool>(player, 158) ||
        field<std::uint8_t>(player, offsets.rejected_9d920e35) ||
        engine::invoke<std::int32_t>(player, 122) <= 0 ||
        engine::invoke<bool>(static_cast<std::byte*>(player) + 8, 9) ||
        field<std::uint8_t>(player, offsets.rejected_cde290d2) ||
        field<std::uint8_t>(player, offsets.rejected_d070684c) ||
        field<std::int32_t>(player, offsets.team) != team)
        return rejected;
    const auto point = center(player);
    const auto along = ((point.y - eye.y) * forward.y + (point.x - eye.x) * forward.x) +
                       (point.z - eye.z) * forward.z;
    if (along < 0.0f || along > 400.0f)
        return rejected;
    const auto dx = point.x - (forward.x * along + eye.x);
    const auto dy = point.y - (forward.y * along + eye.y);
    const auto dz = point.z - (forward.z * along + eye.z);
    return std::sqrt((dx * dx + dy * dy) + dz * dz);
}
struct PositionSample {
    float time;
    MovementVector position;
};
bool history_velocity(void* player, std::ptrdiff_t offset, MovementVector& newest,
                      MovementVector& older) { // complete valid-player433B171A
    const auto* history = static_cast<const std::byte*>(player) + offset;
    if (field<std::uint16_t>(history, 0x10) < 3)
        return false;
    const auto first = field<std::uint16_t>(history, 0xC);
    const auto head = field<std::uint16_t>(history, 0xE);
    const auto* samples = field<const PositionSample*>(history, 8);
    const auto index = [first](std::uint32_t sequence) {
        return sequence >= first ? sequence - first : sequence;
    };
    const auto& a = samples[index(head)];
    const auto& b = samples[index(std::uint32_t(head) + 1u)];
    const auto& c = samples[index(std::uint32_t(head) + 2u)];
    const auto dt = a.time - b.time;
    if (!(dt > 0.0f))
        return false;
    const auto inverse = 1.0f / dt;
    newest = {(a.position.x - b.position.x) * inverse, (a.position.y - b.position.y) * inverse,
              (a.position.z - b.position.z) * inverse};
    const auto previous_dt = b.time - c.time;
    if (!(previous_dt > 0.0f))
        return false;
    const auto previous_inverse = 1.0f / previous_dt;
    older = {(b.position.x - c.position.x) * previous_inverse,
             (b.position.y - c.position.y) * previous_inverse,
             (b.position.z - c.position.z) * previous_inverse};
    return true;
}
MovementAngles perpendicular_angles(MovementVector direction) {
    // Cross with (0,0,1), retaining native signed-zero products.
    const auto x = direction.y - direction.z * 0.0f;
    const auto y = direction.z * 0.0f - direction.x;
    const auto z = direction.x * 0.0f - direction.y * 0.0f;
    if (y == 0.0f && x == 0.0f)
        return {z > 0.0f ? 270.0f : 90.0f, 0.0f, 0.0f};
    const auto yaw =
        static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x))) *
        radians_to_degrees;
    const auto planar = std::sqrt(x * x + y * y);
    const auto pitch = static_cast<float>(std::atan2(static_cast<double>(negate(z)),
                                                     static_cast<double>(planar))) *
                       radians_to_degrees;
    return {pitch, yaw, 0.0f};
}
}
void initialize_blockbot_config(ConfigRegistries& registries, BlockbotConfig& config) noexcept {
    initialize_boolean_config(registries.descriptors, config.enabled, 0xB2CDBB7Du);
    initialize_hotkey_config(registries, config.hotkey, 0xE2FFCCF0u, 0x1Bu, HotkeyMode::hold);
}
void initialize_blockbot_state(BlockbotState& state) noexcept {
    state.target_handle = -1;
    state.flags &= 0xFEu;
    state.locked_right.x =
        std::bit_cast<float>(std::bit_cast<std::uint32_t>(state.locked_right.x) & 0xFFFFFF00u);
    state.activated = 0;
}
void process_blockbot(BlockbotState& state, bool enabled, const HotkeyBinding& hotkey,
                      InputBitmap input, void* player, client::Command& command,
                      MovementAngleView movement_angles, const BlockbotBindings& bindings) {
    state.flags &= 0xFEu;
    const auto& offsets = bindings.offsets;
    const auto deactivate = [&state] { state.activated = 0; };
    if (!enabled || !hotkey_active(hotkey, input)) {
        deactivate();
        return;
    }
    if (field<std::uint8_t>(player, offsets.move_type) != 2 ||
        !(field<std::uint8_t>(player, offsets.ground_flags) & 1u)) {
        deactivate();
        return;
    }
    const auto& move = command.movement;
    if (move.forward_move * move.forward_move + move.side_move * move.side_move != 0.0f ||
        (bindings.game_rules && *bindings.game_rules &&
         field<std::uint8_t>(*bindings.game_rules, offsets.rule_d720aea6))) {
        deactivate();
        return;
    }

    MovementAngles view;
    engine::invoke<void>(bindings.engine_client, 18, &view);
    MovementVector forward;
    movement_angle_vectors({view.pitch, view.yaw, 0.0f}, &forward, nullptr, nullptr);
    const auto team = field<std::int32_t>(player, offsets.team);
    const auto absolute = origin(player);
    const auto eye_offset = field<MovementVector>(player, offsets.view_offset);
    const MovementVector eye{absolute.x + eye_offset.x, absolute.y + eye_offset.y,
                             absolute.z + eye_offset.z};
    void* target = nullptr;
    if (state.target_handle != -1) {
        target = engine::invoke<void*>(bindings.entity_list, 4, state.target_handle);
        if (target && !(target_score(target, team, eye, forward, offsets) <
                        std::numeric_limits<float>::max())) {
            state.target_handle = -1;
            target = nullptr;
        }
    }
    if (!target) {
        auto best = std::numeric_limits<float>::max();
        const auto count = *bindings.max_clients;
        for (std::int32_t i = 1; i <= count; ++i) {
            auto* candidate = engine::invoke<void*>(bindings.entity_list, 3, i);
            if (!candidate)
                continue;
            const auto score = target_score(candidate, team, eye, forward, offsets);
            if (best > score) {
                best = score;
                target = candidate;
                state.target_handle = std::bit_cast<std::int32_t>(
                    *engine::invoke<const std::uint32_t*>(candidate, 2));
            }
        }
    }
    if (!target)
        return;
    if (!state.activated) {
        movement_angle_vectors(view, nullptr, &state.locked_right, nullptr);
        state.activated = 1;
    }
    MovementVector velocity, older;
    if (!history_velocity(target, offsets.position_history, velocity, older))
        return;
    velocity.z = older.z = 0.0f;
    const MovementVector acceleration{velocity.x - older.x, velocity.y - older.y, 0.0f};
    auto position = field<MovementVector>(target, offsets.network_origin);
    const auto ticks = lagcomp::mean_history_timing_ticks(*bindings.timing);
    if (ticks < 100) {
        MovementTraceFlagGuard guard(bindings.trace_flag);
        for (std::int32_t tick = 0; tick < ticks; ++tick) {
            velocity = {velocity.x + acceleration.x, velocity.y + acceleration.y,
                        velocity.z + acceleration.z};
            const auto speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
            const auto maximum_speed = field<float>(target, offsets.maximum_speed);
            if (speed > maximum_speed) {
                const auto factor = maximum_speed / speed;
                velocity = {velocity.x * factor, velocity.y * factor, velocity.z * factor};
            }
            const auto interval = *bindings.interval;
            const auto* maximum = engine::invoke<const MovementVector*>(target, 77);
            const auto* minimum = engine::invoke<const MovementVector*>(target, 76);
            const MovementVector destination{velocity.x * interval + position.x,
                                             velocity.y * interval + position.y,
                                             velocity.z * interval + position.z};
            const auto ray = make_movement_hull_ray(position, destination, *minimum, *maximum);
            MovementCollisionTrace trace{};
            trace.fraction = 1.0f;
            trace.hitgroup = -1;
            trace.hitbox = -1;
            bindings.collision->trace_ray(
                ray, 1,
                {MovementCollisionFilterKind::skip_player,
                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)), 0, 0},
                trace);
            position = trace.end;
            if (trace.fraction != 1.0f)
                break;
        }
    }
    state.flags |= 1u;
    const auto local_origin = origin(player);
    const auto dx = position.x - local_origin.x, dy = position.y - local_origin.y;
    const auto inverse = 1.0f / (std::sqrt((dx * dx + dy * dy) + 0.0f * 0.0f) + epsilon);
    const auto dot =
        ((inverse * dx) * state.locked_right.x + (inverse * dy) * state.locked_right.y) +
        (inverse * 0.0f) * state.locked_right.z;
    if (std::fabs(dot) < 0.001f)
        return;
    auto side = (dot * -450.0f) * 4.0f;
    if (side > 450.0f)
        side = 450.0f;
    else if (-450.0f > side)
        side = -450.0f;
    command.movement.side_move = side;
    movement_angles = perpendicular_angles(state.locked_right);
}
} // namespace skeet
