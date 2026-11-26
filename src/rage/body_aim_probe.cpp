#include "skeet/rage/body_aim_probe.hpp"
#include "skeet/rage/targeting_commit.hpp"
#include "skeet/anti_aim/targets.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <limits>

namespace skeet::rage {
namespace {
constexpr float epsilon = std::numeric_limits<float>::epsilon();
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
MovementVector movement(const RageVector& v) noexcept {
    return {v[0], v[1], v[2]};
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000U);
}
MovementCollisionTrace empty_trace() noexcept {
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    return trace;
}
RageVector transform(const Matrix3x4& bone, const RageVector& v) noexcept {
    RageVector out;
    for (std::size_t row = 0; row < 3; ++row)
        out[row] =
            ((bone.rows[row][1] * v[1] + bone.rows[row][0] * v[0]) + bone.rows[row][2] * v[2]) +
            bone.rows[row][3];
    return out;
}
void flatten(MovementVector& v) noexcept {
    if (v.z == 0.0f)
        return;
    const auto inverse = 1.0f / (std::sqrt((v.x * v.x + v.y * v.y) + 0.0f * 0.0f) + epsilon);
    v = {v.x * inverse, v.y * inverse, 0.0f * inverse};
}
bool gate(const client::CommandFrame& frame, const AcquisitionTargetSettings& settings,
          const BodyAimProbeContext& context) noexcept {
    if (!settings.option(0xDF))
        return false;
    const auto regular = settings.words[1], safe = settings.words[2], combined = regular | safe;
    if (!(combined & 8U) || (context.context_flags & 3U) || frame.weapon_id_d8 == 31)
        return false;
    if ((frame.previous_command.movement.buttons & 2U) || !(frame.flags & 0x40000U))
        return false;
    if (frame.weapon_id_d8 == 64 && !(context.weapon_flags & 1U))
        return false;
    if ((frame.flags & 0x102000U) || (!regular && safe) || !(combined & 0xFFFFFFF3U))
        return false;
    return true;
}
bool probe_origin(const client::CommandFrame& frame, void* target,
                  const BodyAimProbeContext& context, RageVector& origin) {
    auto center = field<RageVector>(target, context.offsets.network_origin);
    const auto* minimum = engine::invoke<const RageVector*>(target, 76);
    const auto* maximum = engine::invoke<const RageVector*>(target, 77);
    center[2] += ((*minimum)[2] + (*maximum)[2]) * 0.5f;
    MovementCollisionRay ray;
    ray.start = movement(frame.origin_dc);
    ray.delta = {center[0] - frame.origin_dc[0], center[1] - frame.origin_dc[1],
                 center[2] - frame.origin_dc[2]};
    ray.start_offset = ray.extents = {0, 0, 0};
    ray.is_ray = true;
    ray.swept =
        (ray.delta.y * ray.delta.y + ray.delta.x * ray.delta.x) + ray.delta.z * ray.delta.z != 0.0f;
    auto trace = empty_trace();
    trace_movement_world(context.collision, ray, 1, trace, context.trace_flag);
    if (!(1.0f > trace.fraction) && !trace.all_solid && !trace.start_solid)
        return false;
    origin = {frame.origin_10c[0], frame.origin_10c[1], frame.origin_10c[2] + 64.0f};
    const auto buttons = frame.previous_command.movement.buttons;
    if (!(frame.scalar_c0 > 2.0f)) {
        if (buttons & 4U)
            return false;
        const auto previous = field<float>(&frame, 0x98);
        return previous > 0.0f && field<float>(&frame, 0x1A4) > previous;
    }
    if (!(buttons & 0x618U))
        return false;
    const auto target_velocity = field<RageVector>(target, context.offsets.velocity);
    if (std::sqrt(target_velocity[0] * target_velocity[0] +
                  target_velocity[1] * target_velocity[1]) > 135.1999969482422f) {
        const auto target_inverse = 1.0f / (std::sqrt(target_velocity[0] * target_velocity[0] +
                                                      target_velocity[1] * target_velocity[1]) +
                                            epsilon);
        const auto local_inverse =
            1.0f / (std::sqrt(frame.velocity_150[0] * frame.velocity_150[0] +
                              frame.velocity_150[1] * frame.velocity_150[1]) +
                    epsilon);
        const auto dot =
            (local_inverse * frame.velocity_150[1]) * (target_inverse * target_velocity[1]) +
            (local_inverse * frame.velocity_150[0]) * (target_inverse * target_velocity[0]);
        if (!(dot >= -0.70710688829422f))
            return false;
    }
    MovementVector forward, right;
    const auto& angles = frame.previous_command.angles;
    movement_angle_vectors({angles[0], angles[1], angles[2]}, &forward, &right, nullptr);
    flatten(forward);
    flatten(right);
    const auto& command = frame.previous_command.movement;
    MovementVector displacement{right.x * command.side_move + forward.x * command.forward_move,
                                right.y * command.side_move + forward.y * command.forward_move,
                                right.z * command.side_move + forward.z * command.forward_move};
    const auto distance = frame.weapon_id_d8 == 9 || frame.weapon_id_d8 == 40 ? 24.0f : 18.0f;
    const auto scale =
        distance / (std::sqrt((displacement.z * displacement.z + displacement.y * displacement.y) +
                              displacement.x * displacement.x) +
                    epsilon);
    displacement = {displacement.x * scale, displacement.y * scale, displacement.z * scale};
    const MovementVector start{frame.origin_10c[0], frame.origin_10c[1],
                               frame.origin_10c[2] + 18.0f};
    const auto hull_minimum = field<MovementVector>(&frame, 0x1B8);
    auto hull_maximum = field<MovementVector>(&frame, 0x1C4);
    hull_maximum.z -= 18.0f;
    ray = make_movement_hull_ray(
        start, {start.x + displacement.x, start.y + displacement.y, start.z + displacement.z},
        hull_minimum, hull_maximum);
    trace = empty_trace();
    context.collision.trace_ray(
        ray, 1, {MovementCollisionFilterKind::skip_player, context.source, 8, 0}, trace);
    if (1.0f > trace.fraction && !trace.start_solid && !trace.all_solid) {
        origin = {(trace.end.x - start.x) + frame.origin_dc[0],
                  (trace.end.y - start.y) + frame.origin_dc[1],
                  (trace.end.z - start.z) + frame.origin_dc[2]};
    } else if (trace.fraction == 1.0f) {
        origin = {displacement.x + frame.origin_dc[0], frame.origin_dc[1] + displacement.y,
                  frame.origin_dc[2] + displacement.z};
    }
    return true;
}
}

std::array<RageVector, 2> body_probe_surface_points(const RageCapsule& capsule,
                                                    const RageVector& origin) noexcept {
    const auto& a = capsule.first;
    const auto& b = capsule.second;
    RageVector direction{(b[0] + a[0]) * 0.5f - origin[0], (b[1] + a[1]) * 0.5f - origin[1],
                         (b[2] + a[2]) * 0.5f - origin[2]};
    const auto inverse =
        1.0f / (std::sqrt((direction[0] * direction[0] + direction[1] * direction[1]) +
                          direction[2] * direction[2]) +
                epsilon);
    direction = {inverse * direction[0], direction[1] * inverse, direction[2] * inverse};
    RageVector perpendicular;
    if (direction[0] == 0.0f && direction[1] == 0.0f)
        perpendicular = {0.0f, -1.0f, 0.0f};
    else {
        const auto zero_z = direction[2] * 0.0f;
        perpendicular = {direction[1] - zero_z, zero_z - direction[0],
                         direction[0] * 0.0f - direction[1] * 0.0f};
        const auto normalize =
            1.0f /
            (std::sqrt((perpendicular[0] * perpendicular[0] + perpendicular[1] * perpendicular[1]) +
                       perpendicular[2] * perpendicular[2]) +
             epsilon);
        perpendicular = {perpendicular[0] * normalize, perpendicular[1] * normalize,
                         perpendicular[2] * normalize};
    }
    const RageVector axis{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    auto along = [&](const RageVector& p) {
        return (axis[1] * p[1] + axis[0] * p[0]) + axis[2] * p[2];
    };
    std::array<RageVector, 2> out;
    const auto& first = along(perpendicular) >= 0.0f ? b : a;
    const auto first_scale =
        capsule.radius /
        std::sqrt((perpendicular[2] * perpendicular[2] + perpendicular[1] * perpendicular[1]) +
                  perpendicular[0] * perpendicular[0]);
    out[0] = {first_scale * perpendicular[0] + first[0], first_scale * perpendicular[1] + first[1],
              first_scale * perpendicular[2] + first[2]};
    perpendicular = {negate(perpendicular[0]), negate(perpendicular[1]), negate(perpendicular[2])};
    const auto& second = along(perpendicular) >= 0.0f ? b : a;
    const auto second_scale =
        capsule.radius /
        std::sqrt((perpendicular[0] * perpendicular[0] + perpendicular[1] * perpendicular[1]) +
                  perpendicular[2] * perpendicular[2]);
    out[1] = {second_scale * perpendicular[0] + second[0],
              second_scale * perpendicular[1] + second[1],
              perpendicular[2] * second_scale + second[2]};
    return out;
}

bool probe_body_aim(client::CommandFrame& frame, AcquisitionTargetSettings& settings, void* target,
                    const BodyAimProbeContext& context) {
    if (!gate(frame, settings, context))
        return false;
    RageVector origin;
    if (!probe_origin(frame, target, context, origin))
        return false;
    auto query = prepare_target_bullet(context.weapon, context.offsets.weapon_item,
                                       context.allow_penetration);
    anti_aim::TargetTraceFilter filter(context.trace_filter_vtable, context.trace_entities);
    const auto binding = filter.binding();
    const auto wrapper = field<const void*>(target, context.offsets.studio_header);
    const std::byte* hitbox = nullptr;
    if (wrapper && field<const void*>(wrapper, 0)) {
        const auto studio = field<const std::byte*>(wrapper, 0);
        const auto set =
            studio + field<std::int32_t>(studio, 0xB0) +
            std::ptrdiff_t(field<std::int32_t>(target, context.offsets.hitbox_set)) * 12;
        if (set && field<std::int32_t>(set, 4))
            hitbox = set + field<std::int32_t>(set, 8) + 6 * 0x44;
    }
    const auto bones = field<const Matrix3x4*>(target, context.offsets.cached_bones);
    const auto& bone = bones[field<std::int32_t>(hitbox, 0)];
    const RageCapsule capsule{transform(bone, field<RageVector>(hitbox, 8)),
                              transform(bone, field<RageVector>(hitbox, 0x14)),
                              field<float>(hitbox, 0x30)};
    const auto points = body_probe_surface_points(capsule, origin);
    const auto target_entity = BallisticEntity(reinterpret_cast<std::uintptr_t>(target));
    std::int32_t maximum_damage = 0;
    for (const auto& point : points) {
        trace_ballistic_bullet(context.ballistics, context.source, origin, point, binding, query);
        if (!_mm_cvttss_si32(_mm_set_ss(query.damage)))
            continue;
        const auto hitgroup = field<std::int32_t>(hitbox, 4);
        const auto info = context.ballistics.entity_info(target_entity);
        const auto damage = final_bullet_damage(query.damage, query.armor_ratio,
                                                std::bit_cast<std::int32_t>(query.reserved),
                                                query.weapon_id, hitgroup, info.armor, info.helmet);
        if (maximum_damage < damage)
            maximum_damage = damage;
    }
    if (!maximum_damage)
        return false;
    auto health = engine::invoke<std::int32_t>(target, 122);
    if (frame.weapon_id_d8 != 40 && frame.field_1d4 == 1 && !(frame.flags & 0x8000000U))
        health = std::bit_cast<std::int32_t>(std::uint32_t(health) - std::uint32_t(maximum_damage));
    const auto minimum_damage = std::bit_cast<std::int32_t>(settings.words[5]);
    const auto required = minimum_damage < health ? health : minimum_damage;
    if (maximum_damage < required)
        return false;
    if (!(context.context_flags & 0x400U) || frame.weapon_id_d8 == 31) {
        settings.words[2] &= 0xCU;
        settings.words[1] &= 0xCU;
        if (!(settings.words[1] | settings.words[2]))
            settings.words[1] = 0xCU;
    }
    frame.flags |= 0x1000000U;
    return true;
}
} // namespace skeet::rage
