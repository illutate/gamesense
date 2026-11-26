#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/esp/grenade_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/collision.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::esp {
namespace {
float length_squared(Vector3 v) {
    return (v.y * v.y + v.x * v.x) + v.z * v.z;
}
Vector3 scaled(Vector3 v, float scale) {
    return {v.x * scale, v.y * scale, v.z * scale};
}
Vector3 difference(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof(result));
    return result;
}
void* entity_pointer(const GrenadeHitEntity& entity) noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(entity.identity));
}
std::uint32_t class_identity(const void* type) noexcept {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(type));
}
GrenadeTrace line(GrenadeState& state, Vector3 start, Vector3 end, std::uint32_t mask,
                  GrenadeTraceProvider provider, std::int32_t group = 0) {
    auto delta = difference(end, start);
    return provider.trace(provider.context, {start, delta, {}, length_squared(delta) != 0, true},
                          mask, {state.owner_entity, 0xffffffffu, group});
}
bool fire_visible(GrenadeState& state, Vector3& origin, Vector3 cell, bool adjust,
                  GrenadeTraceProvider provider) { // 433AC2B0
    auto raised = cell;
    raised.z += 30.0f;
    if (line(state, raised, origin, 0x2004003u, provider).fraction >= 1.0f)
        return true;
    if (line(state, cell, origin, 0x2004003u, provider).fraction >= 1.0f)
        return true;
    if (!adjust)
        return false;
    raised = origin;
    raised.z += 30.0f;
    origin = line(state, origin, raised, 0x2004003u, provider).end;
    return fire_visible(state, origin, cell, false, provider);
}
bool extinguish_cell(GrenadeState& state, Vector3& origin, Vector3 cell,
                     GrenadeTraceProvider provider) {
    cell.z += 40.0f;
    const auto dx = origin.x - cell.x, dy = origin.y - cell.y;
    const auto horizontal = dy * dy + dx * dx;
    if (!(4900.0f > horizontal))
        return false;
    const auto height = std::abs(origin.z - (cell.z - 20.0f)) - 20.0f;
    if (height > 0.0f && height * height + horizontal >= 4900.0f)
        return false;
    if (!fire_visible(state, origin, cell, true, provider))
        return false;
    state.position = origin;
    state.extinguished = true;
    state.velocity = {};
    return true;
}
bool extinguish_fires(GrenadeState& state, Vector3& origin, const GrenadeEnvironment& env,
                      GrenadeTraceProvider provider) {
    for (const auto& fire : env.fires) {
        if (!(262144.0f > length_squared(difference(fire.origin, state.position))))
            continue;
        for (auto cell : fire.cells)
            if (extinguish_cell(state, origin, cell, provider))
                return true;
    }
    return false;
}
bool extinguish_fires(GrenadeState& state, Vector3& origin, const GrenadeEnvironmentBindings& env,
                      GrenadeTraceProvider provider) {
    //4336DC18..DD78 first collects at most64 live inferno pointers. Cell data
    // is read later; intervening visibility traces may mutate the entities.
    std::array<void*, 64> fires;
    std::size_t count = 0;
    const auto last = field<std::int32_t>(env.entity_list, 0x24);
    for (std::int32_t index = 65; index <= last;
         index = std::bit_cast<std::int32_t>(std::uint32_t(index) + 1U)) {
        if (std::uint32_t(index) & 0xFFFF0000U)
            continue;
        auto* entity = engine::invoke<void*>(env.entity_list, 3, index);
        if (!entity)
            continue;
        auto* network = static_cast<std::byte*>(entity) + 8;
        if (engine::invoke<bool>(network, 9))
            continue;
        const auto type = engine::invoke<std::uint32_t>(network, 2);
        if (type != class_identity(env.inferno_class))
            continue;
        const auto position = field<Vector3>(entity, env.offsets.fire_origin_a9ee6062);
        if (!(262144.0f > length_squared(difference(position, state.position))))
            continue;
        fires[count++] = entity;
        if (count == fires.size())
            break;
    }
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const auto* fire = fires[ordinal];
        //4336DDC1..DDD6 retains the field address at SP48 across visibility
        //traces. Its vector contents are read again for each active cell.
        const auto* origin_field =
            static_cast<const std::byte*>(fire) + std::ptrdiff_t(env.offsets.fire_origin_a9ee6062);
        const auto cells = field<std::int32_t>(fire, env.offsets.fire_count_3584e3a5);
        for (std::int32_t index = 0; index < cells; ++index) {
            if (!field<std::uint8_t>(fire, env.offsets.fire_active_fc1ef3a1 + index))
                continue;
            const auto offset = std::ptrdiff_t(index) * 4;
            const auto position = field<Vector3>(origin_field, 0);
            const Vector3 cell{
                float(field<std::int32_t>(fire, env.offsets.fire_x_24a16ff6 + offset)) + position.x,
                float(field<std::int32_t>(fire, env.offsets.fire_y_94e8c7f7 + offset)) + position.y,
                float(field<std::int32_t>(fire, env.offsets.fire_z_d7600aac + offset)) +
                    position.z};
            if (extinguish_cell(state, origin, cell, provider))
                return true;
        }
    }
    return false;
}
template <class Environment>
bool extinguish(GrenadeState& state, const Environment& env, GrenadeTraceProvider provider) {
    auto end = state.position;
    end.z -= 100.0f;
    const auto ground = line(state, state.position, end, 0x200400BU, provider, 1);
    auto origin = state.position;
    if (!ground.start_solid) {
        if (ground.fraction >= 1.0f)
            return false;
        if (ground.fraction > 0.001f)
            origin = ground.end;
    }
    return extinguish_fires(state, origin, env, provider);
}
bool slope_exempt(const GrenadeHitEntity& entity, const GrenadeEnvironment& env) {
    const auto special_physics =
        env.physics_class != 0 && entity.native_class == env.physics_class &&
        entity.solid_type == 1 && entity.movement_type == 7 && (entity.solid_flags & 0x800u) &&
        entity.owner_handle == 0xffffffffu &&
        (entity.collision_activity[0] != 0.0f || entity.collision_activity[1] != 0.0f);
    return entity.identity && entity.native_class &&
           ((entity.flags & 0x28u) || special_physics || entity.incendiary_contact_exempt ||
            entity.native_class == env.breakable_class ||
            entity.native_class == env.alternate_breakable_class);
}
bool special_physics(void* entity, const GrenadeEnvironmentBindings& env) {
    if (!env.physics_class)
        return false;
    auto* network = engine::invoke<void*>(entity, 4);
    if (!network)
        return false;
    const auto type = engine::invoke<std::uint32_t>(network, 2);
    if (!type || type != class_identity(env.physics_class))
        return false;
    auto* collision = engine::invoke<void*>(entity, 3);
    if (engine::invoke<std::int32_t>(collision, 11) != 1)
        return false;
    if (field<std::uint8_t>(entity, env.offsets.movement_type_7501cb91) != 7)
        return false;
    collision = engine::invoke<void*>(entity, 3);
    if (!(engine::invoke<std::uint32_t>(collision, 12) & 0x800U))
        return false;
    if (field<std::uint32_t>(entity, env.offsets.owner_8d1bd8d0) != UINT32_MAX)
        return false;
    return field<float>(entity, env.offsets.activity_8dbc9874) != 0.0f ||
           field<float>(entity, env.offsets.activity_68140cd8) != 0.0f;
}
bool slope_exempt(const GrenadeHitEntity& hit, const GrenadeEnvironmentBindings& env) {
    auto* entity = entity_pointer(hit);
    if (!entity)
        return false;
    auto* network = engine::invoke<void*>(entity, 4);
    if (!network)
        return false;
    const auto type = engine::invoke<std::uint32_t>(network, 2);
    if (!type)
        return false;
    if (field<std::uint32_t>(entity, env.offsets.collision_flags_b7e9c94e) & 0x28U)
        return true;
    if (special_physics(entity, env))
        return true;
    if (type == class_identity(env.breakable_class) ||
        type == class_identity(env.slope_exempt_class))
        return true;
    return engine::invoke<std::int32_t>(entity, 161) != 0;
}
bool collision_player(const GrenadeHitEntity& hit, const GrenadeEnvironment&) noexcept {
    return hit.player;
}
bool collision_player(const GrenadeHitEntity& hit, const GrenadeEnvironmentBindings&) {
    return engine::invoke<bool>(entity_pointer(hit), 158);
}
bool pass_through(const GrenadeHitEntity& entity, const GrenadeEnvironment& env) noexcept {
    if (entity.native_class == env.breakable_class)
        return entity.state == 0;
    if (entity.native_class == env.physics_class)
        return entity.movement_type == 7 && entity.solid_type == 1 &&
               (entity.solid_flags & 0x800U) == 0;
    return entity.native_class == env.alternate_breakable_class && entity.state == 0;
}
bool pass_through(const GrenadeHitEntity& hit, const GrenadeEnvironmentBindings& env) {
    auto* entity = entity_pointer(hit);
    const auto type = engine::invoke<std::uint32_t>(static_cast<std::byte*>(entity) + 8, 2);
    if (type == class_identity(env.breakable_class))
        return field<std::uint8_t>(entity, env.offsets.state_f0374295) == 0;
    if (type == class_identity(env.physics_class)) {
        if (field<std::uint8_t>(entity, env.offsets.movement_type_7501cb91) != 7)
            return false;
        auto* collision = engine::invoke<void*>(entity, 3);
        if (engine::invoke<std::int32_t>(collision, 11) != 1)
            return false;
        collision = engine::invoke<void*>(entity, 3);
        return (engine::invoke<std::uint32_t>(collision, 12) & 0x800U) == 0;
    }
    return type == class_identity(env.alternate_breakable_class) &&
           field<std::uint8_t>(entity, env.offsets.state_f0374295) == 0;
}
std::uint32_t handle(const GrenadeHitEntity& hit, const GrenadeEnvironment&) noexcept {
    return hit.handle;
}
std::uint32_t handle(const GrenadeHitEntity& hit, const GrenadeEnvironmentBindings&) {
    return engine::invoke<std::uint32_t>(static_cast<std::byte*>(entity_pointer(hit)) + 8, 10);
}
float slope(const GrenadeEnvironment& env) noexcept {
    return env.incendiary_max_slope;
}
float slope(const GrenadeEnvironmentBindings& env) noexcept {
    return env.incendiary_max_slope();
}
GrenadeTrace move_collision(GrenadeState& state, Vector3 delta, GrenadeTraceProvider trace,
                            const GrenadeEnvironment&) {
    return grenade_detail::move(state, delta, trace);
}
GrenadeTrace move_collision(GrenadeState& state, Vector3 delta, GrenadeTraceProvider trace,
                            const GrenadeEnvironmentBindings& env) {
    return grenade_detail::move(state, delta, trace, env);
}
}

namespace grenade_detail {
template <class Environment>
void collide_impl(GrenadeState& state, GrenadeTrace& hit, Vector3& displacement,
                  const Environment& env, GrenadeTraceProvider provider) {
    if (state.kind == GrenadeKind::smoke && extinguish(state, env, provider))
        return;
    if ((state.kind == GrenadeKind::molotov || state.kind == GrenadeKind::incendiary) &&
        !slope_exempt(hit.entity, env) &&
        hit.normal.z >= std::cos(slope(env) * std::bit_cast<float>(0x3c8efa35u))) {
        state.velocity = {};
        return;
    }
    float surface_elasticity = 1.0f;
    if (hit.entity.identity) {
        if (collision_player(hit.entity, env)) {
            surface_elasticity = 0.3f;
            if (state.ignored_handle != 0xffffffffu) {
                state.collision_group = 1;
                return;
            }
        } else {
            if (pass_through(hit.entity, env)) {
                state.velocity = scaled(state.velocity, 0.4f);
                state.ignored_handle = handle(hit.entity, env);
                return;
            }
        }
        state.ignored_handle = handle(hit.entity, env);
    }
    const auto elasticity = std::clamp(surface_elasticity * 0.45f, 0.0f, 0.9f);
    const auto backoff = ((hit.normal.y * state.velocity.y + hit.normal.x * state.velocity.x) +
                          hit.normal.z * state.velocity.z) *
                         2.0f;
    auto velocity = state.velocity;
    auto component = [backoff](float value, float normal) {
        auto result = value - normal * backoff;
        if (result > -0.1f && result < 0.1f)
            result = 0.0f;
        return result;
    };
    velocity = {component(velocity.x, hit.normal.x), component(velocity.y, hit.normal.y),
                component(velocity.z, hit.normal.z)};
    velocity = scaled(velocity, elasticity);
    displacement = velocity;
    const auto speed_squared = length_squared(velocity);
    // COMISS/JB admits unordered normal-Z; the later JAE rejects only an
    // ordered speed>=400. Keep those branch semantics in the shared physics.
    if (hit.entity.identity &&
        (!(0.7f >= hit.normal.z) || (hit.normal.z > 0.1f && !(speed_squared >= 400.0f)))) {
        if (speed_squared > 96000.0f) {
            const auto inverse =
                1.0f / (std::sqrt((velocity.y * velocity.y + velocity.z * velocity.z) +
                                  velocity.x * velocity.x) +
                        std::numeric_limits<float>::epsilon());
            const auto alignment =
                ((velocity.y * inverse) * hit.normal.y + (velocity.x * inverse) * hit.normal.x) +
                (velocity.z * inverse) * hit.normal.z;
            if (alignment > 0.5f)
                velocity = scaled(velocity, 1.5f - alignment);
        }
        state.velocity = speed_squared < 400.0f ? Vector3{} : velocity;
        if (!(400.0f > speed_squared)) {
            displacement = scaled(velocity, (1.0f - hit.fraction) * env.tick_interval);
            hit = move_collision(state, displacement, provider, env);
        }
    } else {
        state.velocity = velocity;
        displacement = scaled(velocity, (1.0f - hit.fraction) * env.tick_interval);
        hit = move_collision(state, displacement, provider, env);
    }
    if (state.bounces > 20) {
        state.velocity = {};
        state.next_think_tick = static_cast<std::int32_t>(state.time / env.tick_interval + 0.5f);
    }
}
void collide(GrenadeState& state, GrenadeTrace& hit, Vector3& displacement,
             const GrenadeEnvironment& env, GrenadeTraceProvider provider) {
    collide_impl(state, hit, displacement, env, provider);
}
void collide(GrenadeState& state, GrenadeTrace& hit, Vector3& displacement,
             const GrenadeEnvironmentBindings& env, GrenadeTraceProvider provider) {
    collide_impl(state, hit, displacement, env, provider);
}
} // namespace grenade_detail

void finalize_grenade(GrenadeState& state, GrenadeTraceProvider provider) {
    if (state.finalized)
        return;
    state.finalized = true;
    MovementTraceFlagGuard trace_guard(provider.trace_flag);
    if (state.kind == GrenadeKind::explosive) {
        auto start = state.position;
        start.z += 8.0f;
        auto end = start;
        end.z -= 32.0f;
        auto hit = line(state, start, end, 0x600400bu, provider);
        if (hit.start_solid) {
            start = state.position;
            end = start;
            end.z -= 32.0f;
            hit = line(state, start, end, 0x600400bu, provider);
        }
        if (hit.fraction != 1.0f)
            state.position = {hit.normal.x * 0.6f + hit.end.x, hit.normal.y * 0.6f + hit.end.y,
                              hit.normal.z * 0.6f + hit.end.z};
    } else if (state.kind == GrenadeKind::molotov || state.kind == GrenadeKind::incendiary) {
        state.fire_ground = false;
        if (state.sky)
            return;
        if (state.hit_world) {
            state.fire_ground = true;
            return;
        }
        auto start = state.position;
        start.z += 10.0f;
        auto end = state.position;
        end.z -= 128.0f;
        auto hit = line(state, start, end, 0x200400bu, provider);
        if (hit.fraction != 1.0f && (hit.surface_flags & 4) == 0) {
            state.fire_ground = state.collided = true;
            state.position = hit.end;
            state.last_plane = hit.normal;
        }
    }
}
} // namespace skeet::esp
