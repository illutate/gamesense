#include "skeet/lagcomp/history_velocity_reconstruction.hpp"
#include "skeet/lagcomp/packet_capture.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::lagcomp {
namespace {
using Exit = HistoryVelocityReconstructionExit;
constexpr float epsilon = 0x1p-23f;

template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
float negative(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
std::int32_t difference(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t add_tick(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
float sse_min(float first, float second) noexcept {
    return first < second ? first : second;
}
float sse_max(float first, float second) noexcept {
    return first > second ? first : second;
}
const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto* parent = read<const void*>(object, 0x1C);
        if (parent == object)
            return object;
        object = parent;
    }
}
std::uint32_t convar_bits(const void* object, std::size_t offset) noexcept {
    object = convar_parent(object);
    return read<std::uint32_t>(object, offset) ^
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
float convar_float(const void* object) noexcept {
    return std::bit_cast<float>(convar_bits(object, 0x2C));
}
Vector position(const PacketTimingSample& packet) noexcept {
    return {std::bit_cast<float>(packet.fields_04[0]), std::bit_cast<float>(packet.fields_04[1]),
            std::bit_cast<float>(packet.fields_04[2])};
}
float squared_xy(Vector value) noexcept {
    return value[0] * value[0] + value[1] * value[1];
}
float length_xy(Vector value) noexcept {
    return std::sqrt(squared_xy(value));
}
Vector normalized(Vector value) noexcept {
    const float reciprocal =
        1.0f /
        (std::sqrt((value[0] * value[0] + value[1] * value[1]) + value[2] * value[2]) + epsilon);
    return {value[0] * reciprocal, value[1] * reciprocal, value[2] * reciprocal};
}
Vector perpendicular(Vector value) noexcept {
    if (value[0] == 0.0f && value[1] == 0.0f)
        return {0.0f, -1.0f, 0.0f};
    const float zero_product = value[2] * 0.0f;
    return normalized(
        {value[1] - zero_product, zero_product - value[0], value[0] * 0.0f - value[1] * 0.0f});
}
float dot(Vector a, Vector b) noexcept {
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}
float normalize_yaw(float value) noexcept {
    if (std::isnan(value))
        return 0.0f;
    if (absolute(value) > 180.0f) {
        const float correction = round_half_away_from_zero(absolute(value / 360.0f)) * 360.0f;
        value = value < 0.0f ? value + correction : value - correction;
    }
    return value;
}
float yaw(Vector value, const HistoryVelocityReconstructionBindings& bindings) {
    if (value[0] == 0.0f && value[1] == 0.0f)
        return 0.0f;
    return float(bindings.atan2(double(value[1]), double(value[0]))) * 57.2957763671875f;
}

// Complete43336987: retain the vertical/horizontal special case and both
// slope divisions. A determinant replacement changes native singular cases.
bool circle_center(Vector first, Vector second, Vector third, Vector& center) noexcept {
    const float first_dx = first[0] - second[0];
    const float other_dy = third[1] - first[1];
    if (absolute(first_dx) <= 0.00001f && absolute(other_dy) <= 0.00001f) {
        center = {(third[0] + first[0]) * 0.5f, (first[1] + second[1]) * 0.5f, 0.0f};
        return true;
    }
    const float first_slope = (first[1] - second[1]) / first_dx;
    const float other_slope = other_dy / (third[0] - first[0]);
    if (absolute(first_slope - other_slope) <= 0.000001f)
        return false;
    const float mean_x = (second[0] + first[0]) * 0.5f;
    const float mean_y = (first[1] + second[1]) * 0.5f;
    const float product = other_slope * first_slope;
    const float numerator =
        ((second[1] - third[1]) * product + (second[0] + first[0]) * other_slope) -
        (third[0] + first[0]) * first_slope;
    const float slope_delta = other_slope - first_slope;
    center[0] = numerator / (slope_delta + slope_delta);
    center[1] = mean_y - (center[0] - mean_x) / first_slope;
    center[2] = 0.0f;
    return true;
}

struct FlightWorkspace {
    float duration, gravity, jump_impulse;
    Vector midpoint, end, maximum, minimum;
    MovementCollisionRay ray;
    MovementCollisionTrace trace;
};
void reconstruct_flight(Vector& velocity, bool jump_activity, bool previous_grounded, float delta_z,
                        float height_correction, float packet_duration, std::size_t newest_offset,
                        std::size_t previous_offset, const client::PlayerState& owner, void* player,
                        const HistoryVelocityReconstructionBindings& bindings) {
    if (!jump_activity || previous_grounded) {
        float duration = packet_duration;
        if (jump_activity) {
            const float cycle = read<float>(&owner.native, 0x4F0);
            const float rate = read<float>(&owner.native, 0x4EC);
            duration = maximum_number(bindings.interval,
                                      float(time_to_tick(cycle / rate, bindings.interval)) *
                                          bindings.interval);
        }
        const float gravity_step = negative(convar_float(bindings.cvars.gravity)) * duration;
        const float inferred = (delta_z / duration - gravity_step * 0.5f) + gravity_step;
        velocity[2] = minimum_number(inferred, convar_float(bindings.cvars.jump_impulse));
        return;
    }

    FlightWorkspace work;
    const float cycle = read<float>(&owner.native, 0x4F0);
    const float rate = read<float>(&owner.native, 0x4EC);
    auto ticks = time_to_tick(cycle / rate, bindings.interval);
    if (ticks < 1)
        ticks = 1;
    work.duration = float(ticks) * bindings.interval;
    work.gravity = negative(convar_float(bindings.cvars.gravity));
    work.jump_impulse = convar_float(bindings.cvars.jump_impulse);
    const auto newest = read<Vector>(&owner.native, newest_offset + 4u);
    const auto previous = read<Vector>(&owner.native, previous_offset + 4u);
    work.midpoint = {(previous[0] + newest[0]) * 0.5f, (previous[1] + newest[1]) * 0.5f,
                     (previous[2] + newest[2]) * 0.5f};
    work.end = work.midpoint;
    work.end[2] -= absolute(((work.gravity * 0.5f * work.duration) * work.duration) +
                            work.jump_impulse * work.duration);
    // Native invokes77 before76 and copies their vectors only after both calls.
    const auto* maximum = engine::invoke<const Vector*>(player, 77);
    const auto* minimum = engine::invoke<const Vector*>(player, 76);
    work.maximum = *maximum;
    work.minimum = *minimum;
    work.ray = make_movement_hull_ray({work.midpoint[0], work.midpoint[1], work.midpoint[2]},
                                      {work.end[0], work.end[1], work.end[2]},
                                      {work.minimum[0], work.minimum[1], work.minimum[2]},
                                      {work.maximum[0], work.maximum[1], work.maximum[2]});
    work.trace = {};
    work.trace.fraction = 1.0f;
    work.trace.surface_property = -1;
    work.trace.hitbox = -1;
    // Complete433F545E:434701D0 returns trace type1, unlike434727C0's
    // entity-rejecting type0 filter. Restore the nested guard before policy.
    {
        MovementTraceFlagGuard guard(bindings.trace_flag);
        bindings.trace.trace_ray(work.ray, 1u, {MovementCollisionFilterKind::world_only, 0, 0, 0},
                                 work.trace);
    }
    const bool clear =
        !(work.trace.fraction < 1.0f) && work.trace.all_solid == 0 && work.trace.start_solid == 0;
    if (clear || !(work.trace.plane_normal.z >= 0.7f)) {
        velocity[2] = work.gravity * work.duration + work.jump_impulse;
    } else {
        const float step = work.gravity * work.duration;
        const float current_z = read<float>(&owner.native, newest_offset + 12u);
        const float from_ground =
            ((current_z - work.trace.end.z) - height_correction) / work.duration - step * 0.5f;
        velocity[2] = sse_min(from_ground, step + from_ground);
    }
}

void friction_velocity(Vector& velocity, float elapsed,
                       const HistoryVelocityReconstructionBindings& bindings,
                       bool initial) noexcept {
    const float speed = length_xy(velocity);
    if (initial && !(speed > 0.1f)) {
        velocity = {};
        return;
    }
    const float control = maximum_number(convar_float(bindings.cvars.stop_speed), speed);
    const float drop = (control * convar_float(bindings.cvars.friction)) * elapsed;
    const float reduced = maximum_number(speed - drop, 0.0f);
    if (initial) {
        if (reduced != speed) {
            const float scale = reduced / speed;
            velocity[1] = scale * velocity[1];
            velocity[0] = scale * velocity[0];
        }
    } else {
        const float denominator =
            std::sqrt((velocity[2] * velocity[2] + velocity[1] * velocity[1]) +
                      velocity[0] * velocity[0]) +
            epsilon;
        const float scale = reduced / denominator;
        velocity[0] *= scale;
        velocity[1] *= scale;
        velocity[2] = 0.0f;
    }
}

float accelerate_step(void* player, float projection, float missing,
                      const HistoryVelocityReconstructionBindings& bindings) {
    if (missing <= 0.0f)
        return 0.0f;
    const float positive_projection = sse_max(0.0f, projection);
    const auto flags = (read<std::uint32_t>(player, bindings.fields.player_flags) >> 1) & 1u;
    const bool walking = (read<std::int32_t>(player, bindings.fields.walking) == 1 ||
                          read<std::uint8_t>(player, bindings.fields.walking_override) != 0) &&
                         flags == 0;
    bool exception = false;
    float acceleration_basis = 250.0f;
    float speed_basis = 250.0f;
    if (convar_bits(bindings.cvars.accelerate_weapon_speed, 0x30) != 0u) {
        const auto handle = read<std::uint32_t>(player, bindings.fields.weapon_handle);
        void* weapon =
            handle == UINT32_MAX ? nullptr : engine::invoke<void*>(bindings.entity_list, 4, handle);
        if (weapon) {
            const float maximum_speed = engine::invoke<float>(weapon, 442);
            const auto mode_count = engine::invoke<std::int32_t>(weapon, 462);
            if (mode_count > 0 && engine::invoke<std::int32_t>(weapon, 443) > 1)
                exception = maximum_speed * 0.52f < 110.0f;
            const float ratio = maximum_speed / 250.0f;
            speed_basis = ratio <= 1.0f ? ratio * 250.0f : 250.0f;
            if ((!flags && !walking) || exception)
                acceleration_basis = ratio <= 1.0f ? ratio * 250.0f : 250.0f;
        }
    }
    if (flags) {
        if (!exception)
            acceleration_basis *= 0.34f;
        speed_basis *= 0.34f;
    }
    float acceleration = convar_float(bindings.cvars.accelerate);
    if (walking) {
        if (!exception)
            acceleration_basis *= 0.52f;
        speed_basis *= 0.52f;
        if (positive_projection - speed_basis >= -5.0f) {
            float scale = ((speed_basis * 0.2f - (1.0f - positive_projection * -0.2f)) - -1.0f);
            scale = sse_min(sse_max(scale, 0.0f), 1.0f);
            acceleration = scale * acceleration;
        }
    }
    return sse_min(missing, (bindings.interval * acceleration) * acceleration_basis);
}

struct GroundCandidateWorkspace {
    Vector reference, right, decayed, target_direction;
    Vector direction, velocity, origin, best_velocity;
    std::array<float, 2> speeds;
    float best_distance;
};
void reconstruct_ground_velocity(client::PlayerState& owner, void* player, Vector& velocity,
                                 const HistoryVelocityReconstructionBindings& bindings) {
    if (read<std::uint8_t>(player, bindings.fields.dormant) != 0 || owner.history.count == 0u ||
        length_xy(owner.history.at(0).header.velocity) > 250.0f)
        return;
    const auto old_tick =
        time_to_tick(read<float>(player, bindings.fields.old_simulation_time), bindings.interval);
    const auto index = engine::invoke<std::int32_t>(static_cast<std::byte*>(player) + 8, 10);
    auto& canonical = *bindings.players.get(std::uint32_t(index));
    if (read<float>(player, bindings.fields.simulation_time) -
            read<float>(&canonical.native, 0x19C) >
        10.0f)
        return;

    GroundCandidateWorkspace work;
    work.reference = read<Vector>(&canonical.native, 0x16C);
    work.right = perpendicular(work.reference);
    const float virtual_speed = engine::invoke<float>(player, 276);
    work.speeds[0] =
        minimum_number(read<float>(player, bindings.fields.speed_limit), virtual_speed);
    work.speeds[1] = length_xy(velocity);
    work.best_velocity = velocity;
    work.best_distance = std::numeric_limits<float>::max();
    const auto& initial_record = canonical.history.at(0).header;
    work.decayed = initial_record.velocity;
    float elapsed = read<float>(player, bindings.fields.simulation_time) -
                    float(initial_record.simulation_tick) * bindings.interval;
    elapsed = sse_min(sse_max(elapsed, bindings.interval), bindings.interval * 16.0f);
    friction_velocity(work.decayed, elapsed, bindings, true);
    work.target_direction =
        normalized({velocity[0] - work.decayed[0], velocity[1] - work.decayed[1], 0.0f});
    constexpr std::array<std::array<float, 2>, 8> combinations{{{1.0f, 0.0f},
                                                                {1.0f, -1.0f},
                                                                {0.0f, -1.0f},
                                                                {-1.0f, -1.0f},
                                                                {-1.0f, 0.0f},
                                                                {-1.0f, 1.0f},
                                                                {0.0f, 1.0f},
                                                                {1.0f, 1.0f}}};
    bool close = false;
    for (float speed_limit : work.speeds) {
        for (const auto& combination : combinations) {
            work.direction = normalized(
                {combination[0] * work.right[0] + combination[1] * work.reference[0],
                 combination[0] * work.right[1] + combination[1] * work.reference[1], 0.0f});
            if (!(dot(work.direction, work.target_direction) >= -0.5f))
                continue;
            const auto& record = canonical.history.at(0).header;
            work.velocity = record.velocity;
            // The native scalar z accumulator starts at zero independently
            // of the three words copied from the record at100DF..100E6.
            work.velocity[2] = 0.0f;
            work.origin = record.origin;
            const auto final_tick = time_to_tick(
                read<float>(player, bindings.fields.simulation_time), bindings.interval);
            auto first_tick = add_tick(final_tick, -16);
            if (final_tick < first_tick)
                first_tick = final_tick;
            else if (first_tick <= record.simulation_tick)
                first_tick =
                    final_tick < record.simulation_tick ? final_tick : record.simulation_tick;
            for (auto tick = add_tick(first_tick, 1); tick <= final_tick;
                 tick = add_tick(tick, 1)) {
                friction_velocity(work.velocity, bindings.interval, bindings, false);
                if (tick <= old_tick) {
                    const float projection = dot(work.direction, work.velocity);
                    const float step =
                        accelerate_step(player, projection, speed_limit - projection, bindings);
                    work.velocity[0] += work.direction[0] * step;
                    work.velocity[1] = work.direction[1] * step + work.velocity[1];
                }
                if (length_xy(work.velocity) > speed_limit) {
                    const float reciprocal =
                        speed_limit / (std::sqrt((work.velocity[2] * work.velocity[2] +
                                                  work.velocity[1] * work.velocity[1]) +
                                                 work.velocity[0] * work.velocity[0]) +
                                       epsilon);
                    work.velocity[0] *= reciprocal;
                    work.velocity[1] = reciprocal * work.velocity[1];
                    work.velocity[2] = reciprocal * 0.0f;
                }
                work.origin[0] = bindings.interval * work.velocity[0] + work.origin[0];
                work.origin[1] = bindings.interval * work.velocity[1] + work.origin[1];
                if (read<float>(&canonical.native, 0x3B8) < 0.001f || tick >= final_tick) {
                    const auto live = read<Vector>(player, bindings.fields.origin);
                    const float dx = work.origin[0] - live[0];
                    const float dy = work.origin[1] - live[1];
                    const float distance = dy * dy + dx * dx;
                    if (distance < work.best_distance) {
                        work.best_velocity = work.velocity;
                        work.best_distance = distance;
                        if (distance < 0.0006250000442378223f) {
                            close = true;
                            break;
                        }
                    }
                }
            }
            if (close || work.best_distance < 0.0006250000442378223f) {
                close = true;
                break;
            }
        }
        if (close)
            break;
    }
    if (work.best_distance <= 0.0006250000442378223f) {
        bindings.update_flags |= 8u;
        velocity[0] = work.best_velocity[0];
        velocity[1] = work.best_velocity[1];
    }
}
} // namespace

HistoryVelocityReconstructionExit
reconstruct_history_velocity(client::PlayerState& owner, void* player, Vector& velocity,
                             const HistoryVelocityReconstructionBindings& bindings) {
    const auto timeline = history_timeline(owner);
    const auto& a = timeline.packets[timeline.packet_head % 6u];
    const auto& b = timeline.packets[(timeline.packet_head + 1u) % 6u];
    const auto newest_offset = std::size_t(0x23C) + (timeline.packet_head % 6u) * 32u;
    const auto previous_offset = std::size_t(0x23C) + ((timeline.packet_head + 1u) % 6u) * 32u;
    const auto oldest_offset = std::size_t(0x23C) + ((timeline.packet_head + 2u) % 6u) * 32u;
    const auto newest = position(a), previous = position(b);
    const Vector displacement{newest[0] - previous[0], newest[1] - previous[1],
                              newest[2] - previous[2]};
    if (squared_xy(displacement) > 1000000.0f)
        return Exit::continue_capture;

    bool jump_activity = false, special_sequence = false;
    const float current_cycle = read<float>(&owner.native, 0x4F0);
    const float previous_cycle = read<float>(&owner.native, 0x384);
    const auto sequence = read<std::int32_t>(&owner.native, 0x4DC);
    if (!((current_cycle == previous_cycle &&
           sequence == read<std::int32_t>(&owner.native, 0x370)) ||
          current_cycle >= previous_cycle) &&
        bindings.classify) {
        jump_activity = bindings.classify(player, sequence) == 0x3D9;
        if (jump_activity) {
            const char* name =
                bindings.sequence_name ? bindings.sequence_name(player, sequence) : "";
            special_sequence =
                bindings.compare_sequence_name(name, bindings.jump_sequence_name) != 0;
        }
    }
    const auto flags = a.fields_04[4], previous_flags = b.fields_04[4];
    const bool grounded = (flags & 1u) != 0u;
    float height_correction = 0.0f;
    if (!grounded) {
        const float duck = absolute(read<float>(player, bindings.fields.duck_amount));
        if (duck != read<float>(&owner.native, 0x104) && (duck == 0.0f || duck == 1.0f)) {
            if ((previous_flags & 1u) == 0u) {
                if (((previous_flags ^ flags) & 2u) != 0u)
                    height_correction = (flags & 2u) != 0u ? 9.0f : -9.0f;
            } else if (jump_activity) {
                if (special_sequence && duck == 0.0f)
                    height_correction = -9.0f;
                else if (!special_sequence && duck == 1.0f)
                    height_correction = 9.0f;
            }
        }
    }
    const float delta_z = displacement[2] - height_correction;
    // Native retains the packet addresses across engine classification but
    // rereads these timestamps afterwards; callback changes remain visible.
    const auto newest_tick = read<std::int32_t>(&owner.native, newest_offset);
    const auto previous_tick = read<std::int32_t>(&owner.native, previous_offset);
    const bool old_receive_gap =
        previous_tick < add_tick(read<std::int32_t>(&owner.native, previous_offset + 0x18u), -1);
    const bool new_receive_gap =
        newest_tick < add_tick(read<std::int32_t>(&owner.native, newest_offset + 0x18u), -1);
    const bool receive_transition = old_receive_gap != new_receive_gap;
    float duration = float(difference(newest_tick, previous_tick)) * bindings.interval;
    const Vector old_velocity = read<Vector>(&owner.native, 0x120);
    if (receive_transition) {
        const float speed = length_xy(old_velocity);
        if (speed > 5.0f) {
            const float estimated =
                float(time_to_tick(length_xy(displacement) / speed, bindings.interval)) *
                bindings.interval;
            const auto current_position = read<Vector>(&owner.native, newest_offset + 4u);
            const auto previous_position = read<Vector>(&owner.native, previous_offset + 4u);
            const float dx =
                current_position[0] - (old_velocity[0] * estimated + previous_position[0]);
            const float dy =
                current_position[1] - (old_velocity[1] * estimated + previous_position[1]);
            if (std::sqrt(dy * dy + dx * dx) < 0.1f)
                duration = estimated;
        }
    }
    const auto ticks = time_to_tick(duration, bindings.interval);
    const float reciprocal = 1.0f / duration;
    velocity = {reciprocal * displacement[0], reciprocal * displacement[1], reciprocal * delta_z};
    const float speed_squared = velocity[1] * velocity[1] + velocity[0] * velocity[0];
    if (speed_squared > 2.0f && squared_xy(old_velocity) > 2.0f) {
        const float new_reciprocal = 1.0f / (std::sqrt(speed_squared) + epsilon);
        const float old_reciprocal = 1.0f / (length_xy(old_velocity) + epsilon);
        const float similarity =
            ((old_reciprocal * old_velocity[1]) * (new_reciprocal * velocity[1])) +
            ((new_reciprocal * velocity[0]) * (old_reciprocal * old_velocity[0]));
        if (similarity < 0.9961947202682495f && similarity >= 0.258819043636322f) {
            const float new_yaw = yaw(velocity, bindings);
            const float old_yaw = yaw(old_velocity, bindings);
            const float angle =
                (normalize_yaw(new_yaw - old_yaw) * 0.5f + new_yaw) * 0.01745329238474369f;
            if (!grounded)
                velocity[2] -= convar_float(bindings.cvars.gravity) * duration * 0.5f;
            const float sine = bindings.sine(angle);
            const float cosine = bindings.cosine(angle);
            const float speed = std::sqrt(speed_squared);
            velocity[0] = speed * cosine;
            velocity[1] = speed * sine;
        }
    }
    if (ticks == 1 && !receive_transition)
        return Exit::publish;
    if (ticks <= 0) {
        if (old_receive_gap || !new_receive_gap || jump_activity ||
            ((previous_flags | flags) & 1u) != 0u)
            return Exit::continue_capture;
        if (owner.history.count != 0u) {
            const auto& record = owner.history.at(0).header;
            const float negative_gravity = negative(convar_float(bindings.cvars.gravity));
            velocity = record.velocity;
            const float height =
                read<float>(player, bindings.fields.origin + 8u) - record.origin[2];
            velocity[2] =
                std::sqrt(old_velocity[2] * old_velocity[2] - (height * negative_gravity) * -2.0f);
        }
        return Exit::publish;
    }
    if (ticks > 20)
        return Exit::continue_capture;
    if (grounded) {
        if ((previous_flags & 1u) != 0u)
            reconstruct_ground_velocity(owner, player, velocity, bindings);
        return Exit::publish;
    }

    reconstruct_flight(velocity, jump_activity, (previous_flags & 1u) != 0u, delta_z,
                       height_correction, duration, newest_offset, previous_offset, owner, player,
                       bindings);
    const float reconstructed_squared = squared_xy(velocity);
    if (reconstructed_squared < 400.0f)
        return Exit::publish;
    const auto curve_newest = read<Vector>(&owner.native, newest_offset + 4u);
    const auto curve_previous = read<Vector>(&owner.native, previous_offset + 4u);
    const auto oldest = read<Vector>(&owner.native, oldest_offset + 4u);
    const Vector previous_delta{curve_previous[0] - oldest[0], curve_previous[1] - oldest[1],
                                curve_previous[2] - oldest[2]};
    const float previous_squared =
        previous_delta[1] * previous_delta[1] + previous_delta[0] * previous_delta[0];
    if (previous_squared < 1.0f)
        return Exit::publish;
    if (difference(read<std::int32_t>(&owner.native, previous_offset),
                   read<std::int32_t>(&owner.native, oldest_offset)) > 20 ||
        previous_delta[2] * previous_delta[2] + previous_squared > 100000.0f)
        return Exit::continue_capture;
    Vector center;
    if (!circle_center({curve_previous[0], curve_previous[1], 0.0f},
                       {curve_newest[0], curve_newest[1], 0.0f}, {oldest[0], oldest[1], 0.0f},
                       center))
        return Exit::publish;
    if (!std::isfinite(center[0]) || !std::isfinite(center[1]))
        return Exit::continue_capture;
    const auto delta_direction = normalized(
        {curve_newest[0] - curve_previous[0], curve_newest[1] - curve_previous[1], 0.0f});
    const auto radial_direction =
        normalized({curve_newest[0] - center[0], curve_newest[1] - center[1], 0.0f});
    auto tangent = perpendicular(radial_direction);
    if (!(dot(delta_direction, tangent) >= 0.0f))
        tangent = {negative(tangent[0]), negative(tangent[1]), negative(tangent[2])};
    const float speed = std::sqrt(reconstructed_squared);
    velocity[0] = speed * tangent[0];
    velocity[1] = speed * tangent[1];
    return std::isnan(velocity[0]) || std::isnan(velocity[1]) || std::isnan(velocity[2])
               ? Exit::continue_capture
               : Exit::publish;
}
} // namespace skeet::lagcomp
