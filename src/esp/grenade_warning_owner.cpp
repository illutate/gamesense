#include "skeet/esp/grenade_warning_owner.hpp"
#include "skeet/movement/collision.hpp"
#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace skeet::esp {
namespace {
std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
Vector3 subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float squared(Vector3 v) noexcept {
    return (v.y * v.y + v.x * v.x) + v.z * v.z;
}
float clamp(float value, float low, float high) noexcept {
    // Native MAXSS/MINSS select the second operand for unordered inputs.
    if (!(value > low))
        value = low;
    if (!(value < high))
        value = high;
    return value;
}
bool resolve_thrower(const GrenadeWarningSnapshot& projectile, std::uint32_t type,
                     GrenadeWarningEngine& engine, GrenadeWarningSnapshot& thrower) {
    const auto handle = type < 2 ? projectile.thrower_handle : projectile.owner_handle;
    return handle != 0xffffffffu && engine.snapshot_for_handle(handle, thrower) && thrower.player;
}
Vector3 damage_distance_origin(Vector3 explosion, float remaining,
                               const GrenadeWarningSnapshot& local, GrenadeWarningEngine& engine,
                               GrenadeTraceProvider provider) {
    auto origin = local.virtual_origin;
    if (!((local.velocity.y * local.velocity.y + local.velocity.x * local.velocity.x) > 400.0f))
        return origin;
    const auto time = clamp(remaining, 0.2f, 1.6f);
    const Vector3 predicted{time * local.velocity.x + origin.x, time * local.velocity.y + origin.y,
                            time * local.velocity.z + origin.z};
    if (!(squared(subtract(origin, explosion)) > squared(subtract(predicted, explosion))))
        return origin;
    const auto delta = subtract(predicted, origin);
    MovementTraceFlagGuard guard(provider.trace_flag);
    const auto result = engine.trace({origin, delta, {}, squared(delta) != 0, true}, 1,
                                     {GrenadeDamageFilterKind::ignore_entity, local.identity});
    if (!result.start_solid)
        origin = result.end;
    return origin;
}
void refresh_existing(GrenadeWarningOwner& owner, const GrenadeWarningUpdateFrame& frame,
                      const GrenadeEnvironment& environment, GrenadeWarningEngine& engine,
                      GrenadeTraceProvider trace) {
    for (auto& record : owner.records) {
        GrenadeWarningSnapshot projectile, thrower;
        if (record.handle == 0xffffffffu ||
            !engine.snapshot_for_handle(record.handle, projectile) || projectile.dormant ||
            record.type > 1 || projectile.explosion_effect_tick != 0 ||
            (projectile.flags & 0x20u) || projectile.thrower_handle == 0xffffffffu ||
            !engine.snapshot_for_handle(projectile.thrower_handle, thrower))
            continue;
        owner.projectile_points.clear();
        owner.projectile_points.push_back(projectile.virtual_origin);
        if (!(squared(projectile.velocity) > 900.0f))
            continue;
        const auto current =
            squared(subtract(projectile.velocity, projectile.initial_velocity)) < 1.0f
                ? static_cast<float>(frame.tick_count) * environment.tick_interval
                : projectile.simulation_time;
        GrenadeState state{};
        state.owner_entity = projectile.identity;
        simulate_grenade(state, record.kind, projectile.origin, projectile.velocity, current,
                         record.creation_time, environment, trace, nullptr, nullptr,
                         projectile.collision_group, projectile.projectile_flags);
        finalize_grenade(state, trace);
        if (record.kind == GrenadeKind::explosive)
            state.position.z += 1.0f;
        if (squared(subtract(state.position, record.position)) > 4096.0f)
            record.position = state.position;
    }
}
bool already_recorded(const GrenadeWarningOwner& owner, std::uint32_t index) {
    return std::any_of(owner.records.begin(), owner.records.end(), [index](const auto& record) {
        return (record.handle == 0xffffffffu ? 0x1fffu : record.handle & 0xffffu) == index;
    });
}
}

std::int32_t grenade_warning_team(const GrenadeWarningSnapshot& player,
                                  std::int32_t game_type) noexcept {
    if (player.dormant)
        return player.cached_team_valid ? player.cached_team : player.team;
    return game_type == 6 ? player.survival_team : player.team;
}
bool grenade_warning_expired(const GrenadeWarningRecord& record, bool present,
                             std::int32_t explosion_tick, std::int32_t tick_count,
                             float interval) noexcept {
    if (record.type < 2 && (record.handle == 0xffffffffu || !present || explosion_tick != 0))
        return true;
    return truncate((record.end_time + 1.0f) / interval + 0.5f) < tick_count;
}
bool grenade_warning_admitted(std::uint32_t type, const GrenadeWarningSnapshot* thrower,
                              const GrenadeWarningSnapshot& local,
                              const GrenadeWarningRules& rules) noexcept {
    if (!thrower || !thrower->player)
        return true;
    if (thrower->identity == local.identity)
        return true;
    if (grenade_warning_team(*thrower, rules.game_type) !=
            grenade_warning_team(local, rules.game_type) ||
        rules.teammates_are_enemies)
        return true;
    if (!rules.friendly_fire)
        return false;
    return (type == 0 ? rules.teammate_he_damage : rules.teammate_fire_damage) != 0.0f;
}
float grenade_warning_damage_scale(std::uint32_t type, const GrenadeWarningSnapshot* thrower,
                                   const GrenadeWarningSnapshot& local,
                                   const GrenadeWarningRules& rules) noexcept {
    const auto teammate = thrower && thrower->identity != local.identity &&
                          thrower->team == local.team &&
                          (thrower->index == local.index || !rules.teammates_are_enemies);
    if (teammate && (!rules.friendly_fire || rules.game_player_override))
        return 0.0f;
    if (thrower && thrower->identity == local.identity && type == 0)
        return rules.self_he_damage;
    if (!teammate)
        return 1.0f;
    if (rules.teammate_damage_disabled)
        return 0.0f;
    return type == 0 ? rules.teammate_he_damage : rules.teammate_fire_damage;
}
float grenade_projectile_elapsed(float initial_z, float current_z, float gravity,
                                 float interval) noexcept {
    return static_cast<float>(
               truncate((initial_z - current_z) / ((gravity * 0.4f) * interval) + 0.5f)) *
           interval;
}
bool grenade_warning_smoke_overlap(Vector3 fire, Vector3 smoke) noexcept {
    const auto height = fire.z - smoke.z;
    if (height < -64.0f || height > 120.0f)
        return false;
    auto radius_squared = 22500.0f;
    if (!(height > 72.0f)) {
        if (height < 18.0f) {
            const auto fraction = clamp((height - 12.0f) / -76.0f, 0.0f, 1.0f);
            radius_squared -=
                clamp((fraction + 0.0f) * (fraction + 0.0f) - 0.0f, 0.0f, 1.0f) * 6116.0f;
        }
    } else {
        const auto fraction = clamp((height - 72.0f) / 48.0f, 0.0f, 1.0f);
        radius_squared -=
            clamp((fraction + 0.0f) * (fraction + 0.0f) - 0.0f, 0.0f, 1.0f) * 12500.0f;
    }
    const auto dx = fire.x - smoke.x, dy = fire.y - smoke.y;
    return dy * dy + dx * dx <= radius_squared;
}
void assess_grenade_warning_record(GrenadeWarningRecord& record,
                                   const GrenadeWarningUpdateFrame& frame,
                                   GrenadeWarningEngine& engine, GrenadeTraceProvider trace,
                                   const GrenadeDamageMath& math, std::uint32_t& command_buttons) {
    if (record.type == 0) {
        const auto remaining = record.end_time - frame.current_time;
        auto player = *frame.local_damage;
        player.distance_origin =
            damage_distance_origin(record.position, remaining, *frame.local, engine, trace);
        auto damage = evaluate_he_damage(record.position, player, engine, math).damage;
        if (damage > 0.0f && frame.actual_local) {
            GrenadeWarningSnapshot current_projectile, current_thrower;
            if (record.handle != 0xffffffffu &&
                engine.snapshot_for_handle(record.handle, current_projectile)) {
                const auto current_resolved =
                    resolve_thrower(current_projectile, record.type, engine, current_thrower);
                damage *= grenade_warning_damage_scale(
                    record.type, current_resolved ? &current_thrower : nullptr, *frame.actual_local,
                    frame.rules);
            }
        }
        if (record.damage > damage && remaining > 0.0f && remaining < 0.03125f)
            command_buttons = (command_buttons & 0xfffffbffu) | 0x200u;
        record.damage = damage;
    } else if (record.type == 1) {
        record.suppressed = false;
        std::int32_t collected = 0;
        for (const auto& smoke : engine.partition_entities(record.position, 166.0f, 0x80, false)) {
            if (!smoke.resolved || !smoke.base_entity_present ||
                (frame.classes.smoke_projectile &&
                 smoke.native_class != frame.classes.smoke_projectile))
                continue;
            if (collected++ == 16)
                break;
            if (smoke.smoke_active &&
                grenade_warning_smoke_overlap(record.position, smoke.origin)) {
                record.suppressed = true;
                break;
            }
        }
        record.damage = squared(subtract(record.position, frame.local->virtual_origin)) < 22500.0f
                            ? 100.0f
                            : 0.0f;
    } else if (record.type == 2) {
        record.damage = 0.0f;
        GrenadeWarningSnapshot inferno;
        if (record.handle == 0xffffffffu || !engine.snapshot_for_handle(record.handle, inferno))
            return;
        auto distance = std::sqrt(squared(subtract(frame.local->virtual_origin, inferno.origin)));
        for (const auto& cell : inferno.fire_cells) {
            const auto next = std::sqrt(squared(subtract(cell, frame.local->virtual_origin)));
            distance = _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(next), _mm_set_ss(distance)));
        }
        if (distance < 150.0f)
            record.damage = 100.0f;
    }
}
void update_grenade_warning_records(GrenadeWarningOwner& owner, const GrenadeEspConfig& config,
                                    const GrenadeWarningUpdateFrame& frame,
                                    const GrenadeEnvironment& environment,
                                    GrenadeWarningEngine& engine, GrenadeTraceProvider trace,
                                    const GrenadeDamageMath& math) {
    owner.command_buttons = frame.command_buttons;
    std::erase_if(owner.records, [&](const auto& record) {
        GrenadeWarningSnapshot projectile;
        const auto present = record.type < 2 && record.handle != 0xffffffffu &&
                             engine.snapshot_for_handle(record.handle, projectile);
        return grenade_warning_expired(record, present, projectile.explosion_effect_tick,
                                       frame.tick_count, environment.tick_interval);
    });
    if (!frame.local || (!config.proximity_warning.value && !frame.grenade_entities_enabled))
        return;
    refresh_existing(owner, frame, environment, engine, trace);
    for (const auto& update : frame.packet_updates) {
        if (update.state != 0 || already_recorded(owner, update.index) ||
            static_cast<std::int32_t>(update.index) <= 64 || (update.index & 0xffff0000u))
            continue;
        GrenadeWarningSnapshot projectile, thrower;
        if (!engine.snapshot_for_index(update.index, projectile))
            continue;
        std::uint32_t type;
        GrenadeKind kind;
        float lifetime;
        if (projectile.native_class == frame.classes.base_projectile) {
            if (projectile.damage != 350.0f)
                continue;
            type = 0;
            kind = GrenadeKind::explosive;
            lifetime = 1.5f;
        } else if (projectile.native_class == frame.classes.molotov_projectile) {
            type = 1;
            kind = GrenadeKind::molotov;
            lifetime = environment.incendiary_duration;
        } else if (projectile.native_class == frame.classes.inferno) {
            const auto resolved = resolve_thrower(projectile, 2, engine, thrower);
            if (!grenade_warning_admitted(2, resolved ? &thrower : nullptr, *frame.local,
                                          frame.rules))
                continue;
            const auto start =
                static_cast<float>(projectile.inferno_start_tick) * environment.tick_interval;
            owner.records.push_back({projectile.reference_handle, projectile.origin, 0, 2,
                                     GrenadeKind::molotov, false, start, start + 7.0f});
            continue;
        } else
            continue;
        if (projectile.explosion_effect_tick != 0 || projectile.thrower_handle == 0xffffffffu)
            continue;
        const auto resolved = resolve_thrower(projectile, type, engine, thrower);
        if (!grenade_warning_admitted(type, resolved ? &thrower : nullptr, *frame.local,
                                      frame.rules))
            continue;
        const auto elapsed =
            grenade_projectile_elapsed(projectile.initial_velocity.z, projectile.velocity.z,
                                       environment.gravity, environment.tick_interval);
        if (!(elapsed < lifetime))
            continue;
        const auto creation = projectile.simulation_time - elapsed;
        // Engine virtual+38 is x87. The native owner stores the rounded quotient
        // to a float before CVTTSS2SI, then multiplies by the global tick interval.
        const auto clock_ticks = static_cast<float>(
            static_cast<long double>(frame.engine_time) / frame.engine_clock_interval + 0.5L);
        const auto current = static_cast<float>(truncate(clock_ticks)) * environment.tick_interval;
        GrenadeState state{};
        state.owner_entity = projectile.identity;
        simulate_grenade(state, kind, projectile.origin, projectile.velocity, current, creation,
                         environment, trace, nullptr, nullptr, projectile.collision_group,
                         projectile.projectile_flags);
        GrenadeWarningRecord record{
            projectile.reference_handle, {}, 0, type, kind, false, creation, state.time};
        finalize_grenade(state, trace);
        if (kind == GrenadeKind::explosive)
            state.position.z += 1.0f;
        else if (kind == GrenadeKind::molotov && !state.fire_ground)
            continue;
        record.position = state.position;
        assess_grenade_warning_record(record, frame, engine, trace, math, owner.command_buttons);
        owner.records.push_back(record);
    }
}
} // namespace skeet::esp
