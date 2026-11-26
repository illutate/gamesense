#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/esp/grenade_environment.hpp"
#include "skeet/movement/collision.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace skeet::esp {
namespace {
std::int32_t ticks(float time, float interval) {
    return static_cast<std::int32_t>(time / interval + 0.5f);
}
bool rest_detonation(GrenadeKind kind) {
    return kind == GrenadeKind::smoke || kind == GrenadeKind::decoy ||
           kind == GrenadeKind::tactical;
}
float duration(const GrenadeEnvironment& env) noexcept {
    return env.incendiary_duration;
}
float duration(const GrenadeEnvironmentBindings& env) noexcept {
    return env.incendiary_duration();
}
float gravity(const GrenadeEnvironment& env) noexcept {
    return env.gravity;
}
float gravity(const GrenadeEnvironmentBindings& env) noexcept {
    return env.gravity();
}
std::uint32_t world(const GrenadeEnvironment& env) noexcept {
    return env.world_entity;
}
std::uint32_t world(const GrenadeEnvironmentBindings& env) {
    return env.world_entity();
}
GrenadeTrace move_tick(GrenadeState& state, Vector3 delta, GrenadeTraceProvider trace,
                       const GrenadeEnvironment&) {
    return grenade_detail::move(state, delta, trace);
}
GrenadeTrace move_tick(GrenadeState& state, Vector3 delta, GrenadeTraceProvider trace,
                       const GrenadeEnvironmentBindings& env) {
    return grenade_detail::move(state, delta, trace, env);
}
}
template <class Environment>
void simulate_grenade_impl(GrenadeState& state, GrenadeKind kind, Vector3 origin, Vector3 velocity,
                           float current, float creation, const Environment& env,
                           GrenadeTraceProvider trace, const Projection* projection,
                           GrenadePath* path, std::int32_t collision_group,
                           std::uint16_t projectile_flags, std::vector<Vector3>* world_path) {
    MovementTraceFlagGuard trace_guard(trace.trace_flag);
    const auto owner = state.owner_entity;
    state = {};
    state.owner_entity = owner;
    state.ignored_handle = 0xffffffffu;
    state.collision_group = collision_group;
    state.bounces = std::bit_cast<std::int16_t>(projectile_flags);
    state.kind = kind;
    state.position = origin;
    state.velocity = velocity;
    state.time = current;
    if (rest_detonation(kind))
        state.detonate_time = std::numeric_limits<float>::max();
    else if (kind == GrenadeKind::molotov || kind == GrenadeKind::incendiary)
        state.detonate_time = duration(env) + creation;
    else
        state.detonate_time = creation + 1.5f;
    float think = current == 0.0f ? 0.2f : creation;
    if (current != 0.0f)
        while (think < current && think <= state.detonate_time)
            think = static_cast<float>(ticks(think + 0.2f, env.tick_interval)) * env.tick_interval;
    state.next_think_tick = ticks(think, env.tick_interval);
    auto remaining = ticks(5.0f, env.tick_interval);
    while (remaining-- != 0) {
        if (std::abs(state.velocity.x) < 0.01f && std::abs(state.velocity.y) < 0.01f &&
            std::abs(state.velocity.z) < 0.01f)
            break;
        if (state.next_think_tick <= ticks(state.time, env.tick_interval)) {
            if (state.time > state.detonate_time)
                break;
            if (rest_detonation(state.kind)) {
                const auto speed = std::sqrt(
                    (state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y) +
                    state.velocity.z * state.velocity.z);
                if (speed <= 0.1f)
                    break;
            }
            state.next_think_tick = ticks(state.time + 0.2f, env.tick_interval);
        }
        step_grenade(state, env, trace, projection, path);
        if (path && projection)
            record_grenade_point(*path, *projection, state.position, false);
        // Derived callback4331FC5B used by43380299. Its +64 threshold is zero;
        // the owner supplies the initial point and handles finalized endpoints.
        if (world_path && 0.0f <= state.time)
            world_path->push_back(state.position);
        state.time += env.frame_interval;
    }
}

template <class Environment>
void step_grenade_impl(GrenadeState& state, const Environment& env, GrenadeTraceProvider provider,
                       const Projection* projection, GrenadePath* path) {
    const auto interval = env.frame_interval;
    Vector3 delta{state.velocity.x * interval, state.velocity.y * interval, 0};
    const auto old_z = state.velocity.z;
    state.velocity.z -= (gravity(env) * 0.4f) * interval;
    delta.z = ((state.velocity.z + old_z) * 0.5f) * interval;
    auto hit = move_tick(state, delta, provider, env);
    state.sky = state.hit_world = false;
    if (hit.all_solid)
        state.velocity = {};
    else if (hit.fraction != 1.0f) {
        state.sky = (hit.surface_flags & 4) != 0;
        const auto world_entity = world(env);
        state.hit_world = hit.entity.identity == world_entity;
        state.bounces =
            std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(state.bounces) + 1U));
        state.last_plane = hit.normal;
        state.collided = true;
        if (path && projection)
            record_grenade_point(*path, *projection, hit.end, true);
        grenade_detail::collide(state, hit, delta, env, provider);
    }
}
void simulate_grenade(GrenadeState& state, GrenadeKind kind, Vector3 origin, Vector3 velocity,
                      float current, float creation, const GrenadeEnvironment& env,
                      GrenadeTraceProvider trace, const Projection* projection, GrenadePath* path,
                      std::int32_t group, std::uint16_t flags, std::vector<Vector3>* world_path) {
    simulate_grenade_impl(state, kind, origin, velocity, current, creation, env, trace, projection,
                          path, group, flags, world_path);
}
void simulate_grenade(GrenadeState& state, GrenadeKind kind, Vector3 origin, Vector3 velocity,
                      float current, float creation, const GrenadeEnvironmentBindings& env,
                      GrenadeTraceProvider trace, const Projection* projection, GrenadePath* path,
                      std::int32_t group, std::uint16_t flags, std::vector<Vector3>* world_path) {
    simulate_grenade_impl(state, kind, origin, velocity, current, creation, env, trace, projection,
                          path, group, flags, world_path);
}
void step_grenade(GrenadeState& state, const GrenadeEnvironment& env, GrenadeTraceProvider trace,
                  const Projection* projection, GrenadePath* path) {
    step_grenade_impl(state, env, trace, projection, path);
}
void step_grenade(GrenadeState& state, const GrenadeEnvironmentBindings& env,
                  GrenadeTraceProvider trace, const Projection* projection, GrenadePath* path) {
    step_grenade_impl(state, env, trace, projection, path);
}
} // namespace skeet::esp
