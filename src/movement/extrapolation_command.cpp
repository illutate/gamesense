#include "skeet/movement/extrapolation_command.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/lagcomp/records.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/movement/simulation_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include "movement_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skeet {
using namespace movement_detail;
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
struct LiveCommandPlayer {
    void* player;
    const MovementExtrapolationCommandOffsets& offsets;
    const MovementEnvironmentBindings& environment;
};
const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1c);
        if (parent == object)
            return object;
        object = parent;
    }
}
float convar_float(const void* object) noexcept {
    object = convar_parent(object);
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2c) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
bool convar_integer(const void* object) noexcept {
    object = convar_parent(object);
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
bool walking(const MovementExtrapolationCommandPlayer& player) noexcept {
    return player.field_7090d8dd != 0 || player.field_52899df3 == 1;
}
bool walking(const LiveCommandPlayer& live) noexcept {
    return field<std::uint8_t>(live.player, live.offsets.field_7090d8dd) != 0 ||
           field<std::int32_t>(live.player, live.offsets.field_52899df3) == 1;
}
void infer_walk_button(client::Command& command,
                       const MovementExtrapolationCommandPlayer& player) noexcept {
    if (walking(player))
        command.movement.buttons |= speed_button;
}

// 433F3DC7 uses player flags 2/4 and the two decoded walking fields, rather
// than command buttons or the predicted duck transition used by 433AC9C0.
float player_ground_acceleration(const MovementExtrapolationCommandPlayer& player,
                                 const MovementPhysics& physics, MovementVector velocity,
                                 MovementVector direction, float wish_speed,
                                 const LiveCommandPlayer* live = nullptr) {
    MovementPhysics current_physics = physics;
    float acceleration =
        live ? convar_float(live->environment.convars.accelerate) : physics.accelerate;
    const float current = dot(velocity, direction);
    const float additional = wish_speed - current;
    if (0.0f >= additional)
        return 0.0f;
    bool duck, walk;
    if (live) {
        //433F3E7B captures duck flags before the state-first walking test.
        duck = (field<std::uint32_t>(live->player, live->offsets.player_flags) & 6U) != 0;
        const auto moving = field<std::int32_t>(live->player, live->offsets.field_52899df3) == 1 ||
                            field<std::uint8_t>(live->player, live->offsets.field_7090d8dd) != 0;
        walk = moving && !duck;
        current_physics.weapon_speed_enabled =
            convar_integer(live->environment.convars.accelerate_weapon_speed);
        current_physics.has_weapon = false;
        if (current_physics.weapon_speed_enabled) {
            const auto handle =
                field<std::int32_t>(live->player, live->environment.fields.weapon_handle_cd812654);
            if (handle != -1) {
                const auto weapon = engine::invoke<void*>(live->environment.entity_list, 4, handle);
                if (weapon) {
                    current_physics.has_weapon = true;
                    current_physics.weapon_max_speed = engine::invoke<float>(weapon, 0x6e8U / 4U);
                    current_physics.weapon_mode_count =
                        engine::invoke<std::int32_t>(weapon, 0x738U / 4U);
                    if (current_physics.weapon_mode_count > 0)
                        current_physics.weapon_mode =
                            engine::invoke<std::int32_t>(weapon, 0x6ecU / 4U);
                }
            }
        }
    } else {
        duck = (player.player_flags & 6u) != 0;
        walk = walking(player) && !duck;
    }
    float limit = 250.0f, basis = 250.0f;
    bool exception = false;
    if (current_physics.weapon_speed_enabled && current_physics.has_weapon) {
        exception = weapon_slowdown_exception(current_physics);
        const float scale = current_physics.weapon_max_speed / 250.0f;
        if (1.0f >= scale)
            limit = scale * 250.0f;
        if ((!duck && !walk) || exception) {
            if (1.0f >= scale)
                basis *= scale;
        }
    }
    if (duck) {
        if (!exception)
            basis *= 0.34f;
        limit *= 0.34f;
    }
    if (walk) {
        if (!exception)
            basis *= 0.52f;
        limit *= 0.52f;
        acceleration = walking_acceleration(acceleration, current, limit);
    }
    const auto interval = live ? *live->environment.interval : physics.interval_per_tick;
    const float amount = (interval * acceleration) * basis;
    return sse_min(additional, amount);
}
bool infer_history_command(client::Command& command, const lagcomp::RecordRing& history,
                           float history_time, const MovementExtrapolationCommandPlayer& player,
                           const MovementPhysics& physics, const LiveCommandPlayer* live) {
    if (history.count < 1)
        return false;
    const auto& newest = history.at(0).header;
    const MovementVector velocity{newest.velocity[0], newest.velocity[1], newest.velocity[2]};
    command.angles[1] = planar_movement_yaw(velocity);
    if (history.count < 2)
        return false;
    const auto& previous = history.at(1).header;
    if (newest.duck_amount >= 1.0f || newest.duck_amount > previous.duck_amount)
        command.movement.buttons |= duck_button;
    if (!(newest.player_flags & on_ground))
        return true;
    if (live) {
        if (walking(*live))
            command.movement.buttons |= speed_button;
    } else
        infer_walk_button(command, player);
    const auto interval = live ? *live->environment.interval : physics.interval_per_tick;
    const auto player_time =
        live ? field<float>(live->player, live->offsets.field_d3d4230d) : player.field_d3d4230d;
    if (lagcomp::time_to_tick(player_time, interval) !=
        lagcomp::time_to_tick(history_time, interval))
        return true;

    const float speed = horizontal_length(velocity);
    const float previous_speed = std::sqrt(previous.velocity[0] * previous.velocity[0] +
                                           previous.velocity[1] * previous.velocity[1]);
    const float difference = speed - previous_speed;
    if (5.0f > absolute(difference)) {
        command.movement.forward_move = speed;
        return true;
    }
    float old_x = previous.velocity[0], old_y = previous.velocity[1];
    float decayed = previous_speed;
    if (decayed > 0.1f) {
        const auto delta = subtract(newest.simulation_tick, previous.simulation_tick);
        const auto ticks = delta > 16 ? 16 : delta;
        const auto friction =
            live ? convar_float(live->environment.convars.friction) : physics.friction;
        const float decay_scale = interval * friction;
        for (std::int32_t i = 0; i < ticks; ++i) {
            const auto stop =
                live ? convar_float(live->environment.convars.stop_speed) : physics.stop_speed;
            const auto control = maximum_number(stop, decayed);
            const auto drop = control * decay_scale;
            decayed = maximum_number(decayed - drop, 0.0f);
            if (0.1f > decayed) {
                decayed = 0.0f;
                break;
            }
        }
        if (decayed != previous_speed) {
            const float scale = decayed / previous_speed;
            old_y *= scale;
            old_x *= scale;
        }
    } else
        decayed = 0.0f;
    if (5.0f >= speed - decayed)
        return true;
    const float dx = velocity.x - old_x, dy = velocity.y - old_y;
    const float squared = (dx * dx + dy * dy) + 0.0f * 0.0f;
    const float reciprocal = 1.0f / (std::sqrt(squared) + epsilon);
    const MovementVector direction{reciprocal * dx, reciprocal * dy, reciprocal * 0.0f};
    const MovementVector acceleration_velocity =
        live ? MovementVector{newest.velocity[0], newest.velocity[1], newest.velocity[2]}
             : velocity;
    const auto speed_physics =
        live ? capture_movement_command_physics(live->player, live->environment) : physics;
    const float limit =
        minimum_number(speed_physics.player_speed_limit, speed_physics.player_max_speed);
    const float acceleration =
        player_ground_acceleration(player, physics, acceleration_velocity, direction, limit, live);
    command.movement.forward_move = difference >= acceleration * 0.95f ? limit : speed;
    return true;
}
}

bool infer_history_movement_command(client::Command& command, const lagcomp::RecordRing& history,
                                    float history_time,
                                    const MovementExtrapolationCommandPlayer& player,
                                    const MovementPhysics& physics) noexcept {
    return infer_history_command(command, history, history_time, player, physics, nullptr);
}

bool reconstruct_extrapolation_command(client::Command& command, const lagcomp::RecordRing& history,
                                       float history_time, std::int32_t history_tick,
                                       const MovementExtrapolationCommandPlayer& player,
                                       const MovementPhysics& physics) noexcept {
    std::memset(&command, 0, sizeof command);
    const auto tick = lagcomp::time_to_tick(history_time, physics.interval_per_tick);
    const bool recent = history.count >= 2 && !(tick < subtract(history_tick, 1) &&
                                                tick < history.at(0).header.simulation_tick);
    if (recent)
        return infer_history_movement_command(command, history, history_time, player, physics);
    command.angles[1] = planar_movement_yaw(player.velocity);
    if (player.duck_amount >= 1.0f)
        command.movement.buttons |= duck_button;
    if (player.player_flags & on_ground) {
        infer_walk_button(command, player);
        if (lagcomp::time_to_tick(player.field_d3d4230d, physics.interval_per_tick) == tick)
            command.movement.forward_move = horizontal_length(player.velocity);
    }
    return true;
}

bool reconstruct_live_extrapolation_command(client::Command& command, void* player,
                                            client::PlayerStateCache& players,
                                            const MovementExtrapolationCommandOffsets& offsets,
                                            const MovementEnvironmentBindings& environment) {
    const auto index = engine::invoke<std::int32_t>(static_cast<std::byte*>(player) + 8, 10);
    if (index == 0)
        throw std::logic_error("Movement extrapolation requires a player index");
    auto& owner = *players.get(std::uint32_t(index));
    std::memset(&command, 0, sizeof command);
    const auto& history = owner.history;
    bool recent = false;
    if (history.count >= 2) {
        const auto tick =
            lagcomp::time_to_tick(field<float>(&owner.native, 0x110), *environment.interval);
        recent = !(tick < subtract(field<std::int32_t>(&owner.native, 0x168), 1) &&
                   tick < history.at(0).header.simulation_tick);
    }
    const LiveCommandPlayer live{player, offsets, environment};
    if (recent)
        return infer_history_command(command, history, field<float>(&owner.native, 0x110), {},
                                     MovementPhysics{}, &live);
    const auto velocity = field<MovementVector>(player, offsets.velocity);
    command.angles[1] = planar_movement_yaw(velocity);
    if (field<float>(player, offsets.duck_amount) >= 1.0f)
        command.movement.buttons |= duck_button;
    if (field<std::uint32_t>(player, offsets.player_flags) & on_ground) {
        if (walking(live))
            command.movement.buttons |= speed_button;
        const auto interval = *environment.interval;
        if (lagcomp::time_to_tick(field<float>(player, offsets.field_d3d4230d), interval) ==
            lagcomp::time_to_tick(field<float>(&owner.native, 0x110), interval))
            command.movement.forward_move =
                horizontal_length(field<MovementVector>(player, offsets.velocity));
    }
    return true;
}

} // namespace skeet
