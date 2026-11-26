#include "skeet/rage/ballistics.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace skeet {
namespace {
constexpr std::uint32_t exit_contents = 0x600400bU;
BallisticVector subtract(const BallisticVector& a, const BallisticVector& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float length(const BallisticVector& v) {
    return std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
}
BallisticVector advance(const BallisticVector& p, const BallisticVector& d, float f) {
    return {d[0] * f + p[0], d[1] * f + p[1], d[2] * f + p[2]};
}
BallisticRay ray(const BallisticVector& start, const BallisticVector& end) {
    const auto delta = subtract(end, start);
    return {start, delta,
            (delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2] != 0.0f};
}
bool is_world(BallisticEngine& engine, const BallisticTrace& trace) {
    return trace.entity == engine.entity(0);
}
std::int32_t filter_type(BallisticEngine& engine, const BallisticFilter& filter) {
    // 4346FDD4 uses client GetTraceType 433686FC, a concrete constant 1.
    return filter.kind == BallisticFilterKind::world_only ? 1 : engine.filter_type(filter);
}
bool damageable_model(BallisticEngine& engine, std::int32_t index) {
    if (index <= 0)
        return false;
    const auto model = engine.model_info(index - 1);
    return model.entity_type == 6 && model.model_present && model.model_type == 3 &&
           model.studio_model_present && (model.activity_flags & 0x7fffU) != 0;
}
// 4338590E: negative Euclidean distance outside the finite segment.
float distance_to_segment(const BallisticVector& point, const BallisticVector& start,
                          const BallisticVector& end) {
    const auto d = subtract(end, start), p = subtract(point, start);
    const float segment_length = length(d);
    const float inverse = 1.0f / (segment_length + std::numeric_limits<float>::epsilon());
    const BallisticVector unit{inverse * d[0], inverse * d[1], inverse * d[2]};
    const float projection = (unit[1] * p[1] + unit[0] * p[0]) + unit[2] * p[2];
    if (projection < 0.0f)
        return -length(p);
    if (projection > segment_length)
        return -length(subtract(point, end));
    return length(subtract(point, advance(start, unit, projection)));
}
void clip_one(BallisticEngine& engine, BallisticEntity entity, const BallisticVector& start,
              const BallisticVector& end, float minimum, BallisticTrace& trace, float& fraction) {
    const auto info = engine.entity_info(entity);
    auto center = info.origin;
    center[2] = (info.maximum_z + info.minimum_z) * 0.5f + center[2];
    const float distance = distance_to_segment(center, start, end);
    if (distance < minimum || distance > 60.0f)
        return;
    BallisticTrace clipped;
    engine.clip_ray_to_entity(ray(start, end), 0x4600400bU, entity, clipped);
    if (fraction > clipped.fraction) {
        fraction = clipped.fraction;
        trace = clipped;
    }
}
}

void reset_ballistic_query(BallisticQuery& query) noexcept {
    query.trace.fraction = 1.0f;
    query.trace.hitgroup = -1;
    query.trace.hitbox = -1;
    query.remaining_penetrations = query.allow_penetration ? 4 : 0;
    query.damage = 0.0f;
    query.trace.all_solid = 0;
    query.trace.start_solid = 0;
    query.trace.entity = 0;
    query.point_count = 0;
    query.flags = 0;
}

bool ballistic_breakable(BallisticEngine& engine, BallisticEntity entity) {
    if (!entity)
        return false;
    const auto info = engine.breakable_info(entity);
    if (info.primary_class_matches)
        return info.primary_class_state == 0;
    const auto saved = engine.damage_mode(entity);
    if (info.alternate_class_matches && info.solid_type == 1 && info.collision_group == 7 &&
        (info.solid_flags & 0x800U) && info.health == -1 && !(info.effects & 0x2000000U) &&
        (info.first_scale != 0.0f || info.second_scale != 0.0f))
        engine.set_damage_mode(entity, 2);
    const bool result = engine.has_engine_breakable_callback() && engine.engine_breakable(entity);
    engine.set_damage_mode(entity, saved);
    return result;
}

bool ballistic_enemy(BallisticEngine& engine, BallisticEntity source, BallisticEntity target) {
    if (!source || !target || source == target)
        return false;
    const auto a = engine.entity_info(source), b = engine.entity_info(target);
    const auto rules = engine.rules();
    if (rules.game_type == 6)
        return a.survival_team == -1 || a.survival_team != b.survival_team;
    if (a.team != 2 && a.team != 3)
        return false;
    if ((a.team == 2 && b.team == 3) || (a.team == 3 && b.team == 2))
        return true;
    return rules.teammates_are_enemies;
}

bool trace_ballistic_exit(BallisticEngine& engine, const BallisticVector& direction,
                          const BallisticTrace& entry, const BallisticFilter& filter,
                          BallisticTrace& exit, BallisticEntity source, float step,
                          float maximum_distance) {
    std::uint32_t initial_contents = 0;
    float distance = 0.0f;
    const bool world_only = filter_type(engine, filter) == 1;
    if (!(maximum_distance >= 0.0f))
        return false;
    do {
        distance += step;
        const auto point = advance(entry.end, direction, distance);
        const auto contents = engine.point_contents(point, filter.mask, world_only);
        if (!initial_contents)
            initial_contents = contents;
        if (!(contents & exit_contents) ||
            ((contents & 0x40000000U) && contents != initial_contents)) {
            BallisticTraceRequest request{ray(point, advance(point, direction, -step)),
                                          filter.exit_mask,
                                          filter.kind,
                                          &filter,
                                          source,
                                          0};
            if (filter.exit_mask & 0x40000000U)
                request.filter_kind = BallisticFilterKind::skip_source;
            engine.trace_ray(request, exit);
            if (exit.start_solid && std::int16_t(exit.surface_flags) < 0) {
                request = {ray(point, entry.end),
                           exit_contents,
                           BallisticFilterKind::skip_source_and_hit,
                           &filter,
                           source,
                           exit.entity};
                engine.trace_ray(request, exit);
                if ((exit.fraction < 1.0f || exit.all_solid) && !exit.start_solid)
                    return true;
            } else if ((exit.fraction < 1.0f || exit.all_solid) && !exit.start_solid) {
                if (!(exit.surface_flags & 0x80U) ||
                    (ballistic_breakable(engine, exit.entity) &&
                     ballistic_breakable(engine, entry.entity)) ||
                    (entry.surface_flags & 0x80U))
                    return true;
            } else if (entry.entity && !is_world(engine, entry) &&
                       ballistic_breakable(engine, entry.entity)) {
                exit.surface_property = entry.surface_property;
                exit.end = {direction[0] + entry.end[0], direction[1] + entry.end[1],
                            direction[2] + entry.end[2]};
                return true;
            }
        }
    } while (distance <= maximum_distance);
    return false;
}

float ballistic_exit_budget(float damage, float penetration, const PenetrationSurface& surface,
                            const BallisticTrace& trace, float filter_minimum) noexcept {
    float budget = 90.0f;
    if (!(filter_minimum > 10.0f))
        return budget;
    float combined, damage_modifier;
    if (surface.material == 0x47 || surface.material == 0x59) {
        combined = 3.0f;
        damage_modifier = 0.05f;
    } else {
        combined = (trace.contents & 8U) || (trace.surface_flags & 0x80U)
                       ? 1.0f
                       : (surface.penetration_modifier + 1.0f) * 0.5f;
        damage_modifier = 0.16f;
    }
    if (surface.material == 0x55 || surface.material == 0x57)
        combined = 3.0f;
    else if (surface.material == 0x4c)
        combined = 2.0f;
    const float inverse = maximum_number(0.0f, 1.0f / combined);
    // The pre-trace budget divides in float; the post-trace damage loss uses double.
    const float weapon = maximum_number(0.0f, float(double(3.0f / penetration) * 1.25));
    const float loss =
        float(double(weapon) * (double(inverse) * 3.0) + double(damage * damage_modifier));
    const float available = (damage - (filter_minimum - 1.0f)) - loss;
    const float root = std::sqrt(available) * 4.898979663848877f / std::sqrt(inverse);
    const float magnitude = std::bit_cast<float>(std::bit_cast<std::uint32_t>(root) & 0x7fffffffU);
    const float candidate = (magnitude + 4.0f) + 1.0f;
    return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(candidate), _mm_set_ss(budget)));
}

bool handle_ballistic_penetration(BallisticEngine& engine, const BallisticVector& direction,
                                  BallisticVector& position, BallisticEntity source,
                                  const BallisticFilter& filter, BallisticQuery& query,
                                  const PenetrationSurface& surface, float& damage) {
    const auto& entry = query.trace;
    if (!(query.remaining_penetrations || (entry.contents & 8U) || (entry.surface_flags & 0x80U) ||
          surface.material == 0x59 || surface.material == 0x47) ||
        !(query.penetration > 0.0f) || query.remaining_penetrations <= 0)
        return true;
    const float budget =
        ballistic_exit_budget(damage, query.penetration, surface, entry, filter.minimum_damage);
    BallisticTrace exit;
    if (entry.hitbox > 0 && is_world(engine, entry) && damageable_model(engine, entry.hitbox))
        query.flags |= 2U;
    if (!trace_ballistic_exit(engine, direction, entry, filter, exit, source, 4.0f, budget)) {
        const bool world_only = filter_type(engine, filter) == 1;
        if (!(engine.point_contents(entry.end, exit_contents, world_only) & exit_contents))
            return true;
    }
    const auto exit_surface = engine.surface(exit.surface_property);
    SurfacePenetration penetration{entry.end,
                                   exit.end,
                                   surface,
                                   exit_surface,
                                   (entry.contents & 8U) != 0,
                                   (entry.surface_flags & 0x80U) != 0,
                                   query.penetration};
    BulletPenetrationState state{position, damage, query.remaining_penetrations};
    const bool continued = apply_surface_penetration(state, penetration);
    damage = state.damage;
    if (continued) {
        position = state.position;
        query.remaining_penetrations = state.remaining_penetrations;
    }
    return !continued;
}

void clip_ballistic_entities(BallisticEngine& engine, const BallisticVector& start,
                             const BallisticVector& end, float minimum, float fraction_limit,
                             const BallisticFilter& filter, BallisticTrace& trace) {
    float fraction = fraction_limit;
    if (fraction_limit < 0.0f)
        fraction = trace.fraction;
    if (filter.preferred_entity) {
        clip_one(engine, filter.preferred_entity, start, end, minimum, trace, fraction);
        return;
    }
    const auto count = engine.maximum_clients();
    for (std::int32_t index = 1; index <= count; ++index) {
        const auto entity = engine.entity(index);
        if (!entity)
            continue;
        const auto info = engine.clip_entity_info(entity);
        if (info.virtual_278 && !info.dormant && !info.target_state &&
            filter.kind != BallisticFilterKind::world_only &&
            engine.accepts(filter, entity, 0x4600400bU))
            clip_one(engine, entity, start, end, minimum, trace, fraction);
    }
}

std::int32_t trace_ballistic_bullet(BallisticEngine& engine, BallisticEntity source,
                                    const BallisticVector& start, const BallisticVector& target,
                                    const BallisticFilter& filter, BallisticQuery& query) {
    reset_ballistic_query(query);
    engine.bind_source(filter, source);
    engine.bind_last_hit(filter, 0);
    const auto delta = subtract(target, start);
    const float distance = length(delta);
    const float inverse = 1.0f / (distance + std::numeric_limits<float>::epsilon());
    const BallisticVector direction{inverse * delta[0], inverse * delta[1], inverse * delta[2]};
    float damage = query.base_damage;
    const float minimum = maximum_number(1.0f, filter.minimum_damage - 5.0f);
    if (distance > query.range + 16.0f)
        return 0;
    if (minimum > damage) {
        query.damage = 0.0f;
        return 0;
    }
    const float endpoint_distance = distance - 0.1f;
    BallisticVector position = start;
    float travelled = 0.0f;
    for (;;) {
        const float remaining = query.range - travelled;
        const auto end = advance(position, direction, remaining);
        engine.trace_ray({ray(position, end), filter.mask, filter.kind, &filter, source, 0},
                         query.trace);
        if (filter.mask & 0x40000000U) {
            const auto extended = advance(end, direction, 40.0f);
            const float fraction =
                length(subtract(query.trace.end, position)) / length(subtract(position, extended));
            clip_ballistic_entities(engine, position, extended, 0.0f, fraction, filter,
                                    query.trace);
        }
        if (length(subtract(query.trace.end, start)) >= endpoint_distance) {
            const float fraction =
                length(subtract(position, target)) / length(subtract(position, end));
            const float exponent = (fraction * remaining + travelled) * 0.002f;
            damage =
                float(std::pow(double(query.range_modifier), double(exponent)) * double(damage));
            query.trace.end = target;
            break;
        }
        travelled = query.trace.fraction * remaining + travelled;
        damage = float(std::pow(double(query.range_modifier), double(travelled * 0.002f)) *
                       double(damage));
        if (query.trace.fraction == 1.0f)
            break;
        query.points[query.point_count++] = query.trace.end;
        if (minimum > damage) {
            damage = 0.0f;
            break;
        }
        const auto surface = engine.surface(query.trace.surface_property);
        if ((travelled > 3000.0f && query.penetration > 0.0f) ||
            double(surface.penetration_modifier) < 0.1)
            query.remaining_penetrations = 0;
        if (!(filter.mask & 0x40000000U))
            query.trace.hitgroup = 1;
        BallisticEntity hit = 0;
        BallisticEntityInfo hit_info{};
        if (query.trace.entity) {
            hit_info = engine.entity_info(query.trace.entity);
            if (hit_info.virtual_278)
                hit = query.trace.entity;
        }
        engine.bind_last_hit(filter, hit);
        if (query.trace.entity && hit_info.virtual_280) {
            if (!hit) {
                damage = 0.0f;
                break;
            }
            const auto& info = hit_info;
            if (!info.target_state) {
                if (info.target_index_blocked || (source && !ballistic_enemy(engine, source, hit)))
                    damage = 0.0f;
                else if ((filter.mask & 0x40000000U) &&
                         std::uint32_t(query.trace.hitgroup) - 1U <= 7U)
                    damage = hitgroup_damage_scale(info.armor, info.helmet, query.trace.hitgroup,
                                                   query.weapon_id, query.armor_ratio) *
                             damage;
                break;
            }
        }
        if (query.remaining_penetrations <= 0) {
            damage = 0.0f;
            break;
        }
        if (handle_ballistic_penetration(engine, direction, position, source, filter, query,
                                         surface, damage)) {
            query.flags |= 4U;
            damage = 0.0f;
            break;
        }
        if (minimum > damage) {
            damage = 0.0f;
            break;
        }
    }
    if (damage < 0.0f)
        damage = 0.0f;
    query.damage = damage;
    return _mm_cvttss_si32(_mm_set_ss(damage));
}
} // namespace skeet
