#include "skeet/movement/simulation_collision.hpp"
#include "movement_math.hpp"

namespace skeet {
namespace {
using namespace movement_detail;

void trace_slide(MovementSimulation& state, MovementCollisionEngine& engine, MovementVector start,
                 MovementVector end, MovementCollisionTrace& trace) {
    engine.trace_ray(make_movement_hull_ray(start, end, state.hull_minimum, state.hull_maximum),
                     state.collision_mask,
                     {MovementCollisionFilterKind::skip_player, state.player, 8, 0}, trace);
}

void record_plane(MovementSimulation& state, MovementVector normal) noexcept {
    const auto original = state.simulation_flags;
    state.simulation_flags = original | 8u;
    state.contact_position = state.position;
    if (normal.z >= 0.7f)
        state.simulation_flags = original | 0xcu;
    else if (0.1f > absolute(normal.z))
        state.simulation_flags = original | 0xau;
    else
        return;
    state.contact_normal = normal;
}

MovementVector clip_velocity(MovementVector velocity, MovementVector normal) noexcept {
    const float amount = (normal.x * velocity.x + normal.y * velocity.y) + velocity.z * normal.z;
    MovementVector result{velocity.x - normal.x * amount, velocity.y - normal.y * amount,
                          velocity.z - normal.z * amount};
    const float residual = (normal.y * result.y + normal.x * result.x) + result.z * normal.z;
    if (!(residual >= 0.0f)) {
        const float correction = minimum_number(residual, -0.03125f);
        result.x -= normal.x * correction;
        result.y -= normal.y * correction;
        result.z -= correction * normal.z;
    }
    return result;
}

MovementVector crease_velocity(MovementVector velocity, MovementVector a,
                               MovementVector b) noexcept {
    const MovementVector direction{a.y * b.z - a.z * b.y, b.x * a.z - a.x * b.z,
                                   a.x * b.y - b.x * a.y};
    const float length = std::sqrt((direction.x * direction.x + direction.y * direction.y) +
                                   direction.z * direction.z);
    const float factor = 1.0f / (length + epsilon);
    const MovementVector normalized{factor * direction.x, factor * direction.y,
                                    factor * direction.z};
    const float along =
        (velocity.y * normalized.y + normalized.x * velocity.x) + velocity.z * normalized.z;
    return {along * normalized.x, normalized.y * along, normalized.z * along};
}
}

void slide_movement_simulation(MovementSimulation& state, MovementCollisionEngine& engine,
                               const MovementVector* first_destination) {
    using namespace movement_detail;
    const MovementVector original_velocity = state.velocity;
    MovementVector anchor = state.velocity;
    const MovementVector reuse = first_destination ? *first_destination : MovementVector{};
    std::array<MovementVector, 5> planes;
    unsigned count = 0;
    float remaining = state.rules.interval_per_tick;
    float fraction_sum = 0.0f;
    for (unsigned bump = 0; bump < 4; ++bump) {
        const float squared =
            (state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y) +
            state.velocity.z * state.velocity.z;
        if (squared == 0.0f)
            break;
        const MovementVector end{remaining * state.velocity.x + state.position.x,
                                 remaining * state.velocity.y + state.position.y,
                                 remaining * state.velocity.z + state.position.z};
        if (!(first_destination && end.x == reuse.x && end.y == reuse.y && end.z == reuse.z))
            trace_slide(state, engine, state.position, end, state.trace);
        float fraction = state.trace.fraction;
        if (fraction > 0.0f && fraction < 0.0001f)
            state.trace.fraction = fraction = 0.0f;
        fraction_sum += fraction;
        if (state.trace.all_solid) {
            state.velocity = {};
            break;
        }
        if (fraction > 0.0f) {
            if (fraction == 1.0f) {
                MovementCollisionTrace probe{};
                probe.fraction = 1.0f;
                probe.hitgroup = probe.hitbox = -1;
                trace_slide(state, engine, state.trace.end, state.trace.end, probe);
                if (probe.start_solid || probe.fraction != 1.0f) {
                    state.velocity = {};
                    break;
                }
            }
            state.position = state.trace.end;
            anchor = state.velocity;
            count = 0;
        }
        if (fraction == 1.0f)
            break;
        if (!(absolute(state.trace.plane_normal.z) >= 0.0001f))
            state.trace.plane_normal.z = 0.0f;
        remaining -= remaining * fraction;
        if (count > 4) {
            state.velocity = {};
            break;
        }
        planes[count++] = state.trace.plane_normal;
        if (count == 1 && state.movement_type == 2 && !(state.player_flags & on_ground)) {
            record_plane(state, planes[0]);
            anchor = clip_velocity(anchor, planes[0]);
            state.velocity = anchor;
        } else {
            unsigned selected = 0;
            for (; selected < count; ++selected) {
                record_plane(state, planes[selected]);
                state.velocity = clip_velocity(anchor, planes[selected]);
                unsigned other = 0;
                for (; other < count; ++other)
                    if (other != selected && dot(state.velocity, planes[other]) < 0.0f)
                        break;
                if (other == count)
                    break;
            }
            if (selected == count) {
                if (count != 2) {
                    state.velocity = {};
                    break;
                }
                state.velocity = crease_velocity(state.velocity, planes[0], planes[1]);
            }
            if (!(dot(state.velocity, original_velocity) > 0.0f)) {
                state.velocity = {};
                break;
            }
        }
    }
    if (fraction_sum == 0.0f)
        state.velocity = {};
}

} // namespace skeet
