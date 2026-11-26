#include "skeet/esp/grenade_damage.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <cstring>

namespace skeet::esp {
namespace {
Vector3 subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float length_xyz(Vector3 v) {
    return std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
}
float squared_yxz(Vector3 v) {
    return (v.y * v.y + v.x * v.x) + v.z * v.z;
}
GrenadeRay line(Vector3 start, Vector3 end) {
    const auto delta = subtract(end, start);
    return {start, delta, {0, 0, 0}, squared_yxz(delta) != 0.0f, true};
}
float negative(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000U);
}
}
std::array<Vector3, 5> make_grenade_damage_samples(Vector3 origin, Vector3 eye, float eye_offset,
                                                   Vector3 top, Vector3 angles,
                                                   const GrenadeDamageMath& math) {
    const auto& trig = math.trigonometry;
    constexpr float radians = 0.01745329238474369f;
    const float sy = trig.sine(angles.y * radians);
    const float cy = trig.cosine(angles.y * radians);
    const float sp = trig.sine(angles.x * radians);
    const float cp = trig.cosine(angles.x * radians);
    const float sr = trig.sine(angles.z * radians);
    const float cr = trig.cosine(angles.z * radians);
    const float cr_negative = cr * -1.0f;
    const float srsp = sr * sp;
    const float right_x = negative(sy) * cr_negative - srsp * cy;
    const float right_y = cr_negative * cy - srsp * sy;
    const float right_z = (sr * -1.0f) * cp;
    const float right_length =
        std::sqrt((right_x * right_x + right_y * right_y) + right_z * right_z);
    const float scale = 16.0f / (right_length + std::numeric_limits<float>::epsilon());
    const float x = scale * right_x, y = scale * right_y;
    const float sample_z = origin.z + eye_offset;
    return {eye, top, origin, Vector3{origin.x - x, origin.y - y, sample_z},
            Vector3{x + origin.x, y + origin.y, sample_z}};
}
namespace {
template <bool live>
float exposure(Vector3 position, std::uint32_t target, const std::array<Vector3, 5>& samples,
               const void* entity, const engine::DecodedOffset* origin_offset,
               GrenadeDamageEngine& engine) {
    const float first = grenade_surface_transmission(position, samples[0], target, engine) * 0.4f;
    const float second =
        grenade_surface_transmission(position, samples[1], target, engine) * 0.2f + first;
    auto third_sample = samples[2];
    if constexpr (live)
        std::memcpy(&third_sample,
                    static_cast<const std::byte*>(entity) + std::ptrdiff_t(*origin_offset),
                    sizeof third_sample);
    const float third =
        grenade_surface_transmission(position, third_sample, target, engine) * 0.2f + second;
    const float fourth =
        grenade_surface_transmission(position, samples[3], target, engine) * 0.1f + third;
    return grenade_surface_transmission(position, samples[4], target, engine) * 0.1f + fourth;
}
// Exact 13 vectors at 434745F0..4347468C. The missing eighth diagonal is
// intentional; synthesizing a symmetric fourteen-direction set changes it.
constexpr std::array<Vector3, 13> offset_directions{{{1, 0, 0},
                                                     {-1, 0, 0},
                                                     {0, 1, 0},
                                                     {0, -1, 0},
                                                     {0, 0, 1},
                                                     {0, 0, -1},
                                                     {1, 1, 1},
                                                     {1, -1, 1},
                                                     {-1, 1, 1},
                                                     {-1, -1, 1},
                                                     {1, 1, -1},
                                                     {1, -1, -1},
                                                     {-1, -1, -1}}};
}

bool grenade_damage_should_hit(GrenadeDamageFilterKind kind,
                               const GrenadeDamageFilterEntity& entity) noexcept {
    if (!entity.base_filter_accepts)
        return false;
    if (kind == GrenadeDamageFilterKind::ignore_entity)
        return true;
    return entity.converted_entity_present && !entity.virtual_278 && !entity.virtual_298;
}
float grenade_player_view_height(float duck) noexcept {
    const float squared = duck * duck;
    const float doubled = squared + squared;
    const float smooth = squared * 3.0f - doubled * duck;
    return (1.0f - smooth) * 18.0f + 46.0f;
}
float grenade_surface_transmission(Vector3 explosion, Vector3 sample, std::uint32_t target,
                                   GrenadeDamageEngine& engine) {
    const auto ray = line(explosion, sample);
    auto trace = engine.trace(ray, 0x46004003U, {GrenadeDamageFilterKind::ignore_entity, target});
    if (trace.fraction == 1.0f)
        return 1.0f;
    const auto world = engine.world_entity();
    const auto obstacle = trace.entity;
    if (obstacle == world || !obstacle || obstacle == target)
        return 0.0f;
    const auto owner = engine.owner_handle(obstacle);
    const auto owner_entity = owner == 0xffffffffU ? 0U : engine.entity_for_handle(owner);
    if (owner_entity == target)
        return 0.0f;
    trace = engine.trace(ray, 1U, {GrenadeDamageFilterKind::ignore_entity, 0});
    if (trace.fraction != 1.0f)
        return 0.0f;
    const auto object = engine.physics_object(obstacle);
    if (!object)
        return 0.75f;
    const auto material = engine.physics_material(object);
    const float normalized = engine.surface_density(material) / 3000.0f;
    if (normalized < 0.0f)
        return 1.0f;
    if (normalized >= 0.0f && normalized < 1.0f)
        return float(1.0 - double(normalized));
    return 0.0f;
}
namespace {
template <bool live>
GrenadeDamageResult evaluate_geometry(Vector3 explosion, std::uint32_t target,
                                      Vector3 distance_point, const std::array<Vector3, 5>& samples,
                                      const void* entity,
                                      const engine::DecodedOffset* origin_offset,
                                      GrenadeDamageEngine& engine, const GrenadeDamageMath& math) {
    GrenadeDamageResult result;
    result.sample_position = explosion;
    result.distance_point = distance_point;
    const auto delta = subtract(result.distance_point, explosion);
    if (!(squared_yxz(delta) < 122500.0f))
        return result;
    result.in_range = true;
    result.exposure = exposure<live>(explosion, target, samples, entity, origin_offset, engine);
    if (result.exposure < 1.0f) {
        float best = result.exposure;
        auto best_position = explosion;
        for (const auto& direction : offset_directions) {
            const float inverse =
                1.0f / (length_xyz(direction) + std::numeric_limits<float>::epsilon());
            const Vector3 end{(direction.x * inverse) * 8.0f + explosion.x,
                              (direction.y * inverse) * 8.0f + explosion.y,
                              (direction.z * inverse) * 8.0f + explosion.z};
            const auto trace = engine.trace(line(explosion, end), 0x42004003U,
                                            {GrenadeDamageFilterKind::exclude_players, target});
            auto candidate = trace.end;
            if (trace.fraction < 1.0f || trace.all_solid || trace.start_solid)
                candidate = {(explosion.x - trace.end.x) * 0.05f + trace.end.x,
                             (explosion.y - trace.end.y) * 0.05f + trace.end.y,
                             (explosion.z - trace.end.z) * 0.05f + trace.end.z};
            const float next =
                exposure<live>(candidate, target, samples, entity, origin_offset, engine);
            if (next > best) {
                best = next;
                best_position = candidate;
            }
        }
        if (best > result.exposure) {
            result.exposure = best;
            result.sample_position = best_position;
        }
    }
    if (result.exposure > 0.0f) {
        const float distance = length_xyz(subtract(result.distance_point, result.sample_position));
        const float exponent = (negative(distance) * distance) / 27222.220703125f;
        result.damage = (math.exponential(exponent) * 99.0f) * result.exposure;
    }
    return result;
}
}
GrenadeDamageResult evaluate_he_damage_geometry(Vector3 explosion, std::uint32_t target,
                                                Vector3 distance_point,
                                                const std::array<Vector3, 5>& samples,
                                                GrenadeDamageEngine& engine,
                                                const GrenadeDamageMath& math) {
    return evaluate_geometry<false>(explosion, target, distance_point, samples, nullptr, nullptr,
                                    engine, math);
}
GrenadeDamageResult
evaluate_he_damage_live_geometry(Vector3 explosion, std::uint32_t target, Vector3 distance_point,
                                 const std::array<Vector3, 5>& samples, const void* entity,
                                 const engine::DecodedOffset& origin_offset,
                                 GrenadeDamageEngine& engine, const GrenadeDamageMath& math) {
    return evaluate_geometry<true>(explosion, target, distance_point, samples, entity,
                                   &origin_offset, engine, math);
}
GrenadeDamageResult evaluate_he_damage(Vector3 explosion, const GrenadeDamagePlayer& player,
                                       GrenadeDamageEngine& engine, const GrenadeDamageMath& math) {
    GrenadeDamageResult result;
    result.sample_position = explosion;
    const float height = grenade_player_view_height(player.duck_amount) * 0.85f;
    result.distance_point = {player.distance_origin.x, player.distance_origin.y,
                             height + player.distance_origin.z};
    if (!(squared_yxz(subtract(result.distance_point, explosion)) < 122500.0f))
        return result;
    Vector3 eye;
    if (player.alternate_eye)
        eye = {player.view_offset.x + player.virtual_origin.x,
               player.virtual_origin.y + player.view_offset.y,
               player.virtual_origin.z + player.view_offset.z};
    else
        eye = {player.origin.x, player.origin.y,
               grenade_player_view_height(player.duck_amount) + player.origin.z};
    result = evaluate_he_damage_geometry(
        explosion, player.entity, result.distance_point,
        make_grenade_damage_samples(player.origin, eye, eye.z - player.origin.z,
                                    {player.origin.x, player.origin.y, player.origin.z + 71.0f},
                                    player.absolute_angles, math),
        engine, math);
    if (result.exposure > 0.0f && player.armor > 0)
        result.damage *= 0.6f;
    return result;
}
} // namespace skeet::esp
