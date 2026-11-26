#include "skeet/anti_aim/edge.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>
#include <limits>

namespace skeet::anti_aim {
namespace {
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu);
}
float clamp(float value, float low, float high) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(low)), _mm_set_ss(high)));
}
bool blocked(const MovementCollisionTrace& trace) noexcept {
    return trace.fraction < 1.0f || trace.all_solid || trace.start_solid;
}
float angle_difference(float first, float second) {
    const auto delta = first - second;
    auto result = static_cast<float>(std::fmod(static_cast<double>(delta), 360.0));
    if (first > second) {
        if (result >= 180.0f)
            result -= 360.0f;
    } else if (result <= -180.0f)
        result += 360.0f;
    return result;
}
void initial_view_basis(MovementAngles angles) {
    constexpr float radians = 0.01745329238474369049072265625f;
    const auto yaw = angles.yaw * radians;
    const auto sy = std::sin(yaw), cy = std::cos(yaw);
    const auto pitch = angles.pitch * radians;
    const volatile auto sp = std::sin(pitch);
    (void)sp;
    const auto cp = std::cos(pitch);
    const auto x = cp * cy, y = cp * sy;
    const auto inverse =
        1.0f / (std::sqrt((x * x + y * y) + 0.0f) + std::numeric_limits<float>::epsilon());
    const auto forward_x = x * inverse, forward_y = y * inverse;
    const auto forward_z = inverse * 0.0f;
    if (forward_x != 0.0f || forward_y != 0.0f) {
        const auto common = forward_z * 0.0f;
        const auto side_x = forward_y - common, side_y = common - forward_x;
        const auto side_z = forward_x * 0.0f - forward_y * 0.0f;
        // The original stores this temporary before the radial loop reuses it.
        const volatile auto side_length =
            std::sqrt((side_x * side_x + side_y * side_y) + side_z * side_z);
        (void)side_length;
    }
}
}

bool solve_edge_yaw(const EdgeInput& input, const EdgeSettings& settings, EdgeEngine& engine,
                    std::uint8_t* trace_flag, float& command_yaw) {
    if (!input.enabled || input.frame_scalar_c0 >= input.player_scalar || input.movement_type == 9)
        return false;
    const auto& player_minimum = engine.hull_minimum();
    const auto& player_maximum = engine.hull_maximum();
    const auto padding = settings.hull_padding;
    const MovementVector minimum{player_minimum.x - padding, player_minimum.y - padding, -8.0f};
    const MovementVector maximum{player_maximum.x + padding, player_maximum.y + padding, 8.0f};
    auto origin = engine.origin();
    origin.z += 56.0f;
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace_movement_world(engine, make_movement_hull_ray(origin, origin, minimum, maximum), 1, trace,
                         trace_flag);
    if (!blocked(trace))
        return false;
    const auto first_view = engine.view_angles();
    initial_view_basis(first_view);

    std::int32_t sample_count = 0, normal_count = 0;
    MovementVector total{}, normals{};
    for (float angle = 0.0f; angle < std::bit_cast<float>(0x40C90FDBu);
         angle += std::bit_cast<float>(0x3EA0D97Cu)) {
        const auto sine = std::sin(angle), cosine = std::cos(angle); // complete 0x433C84EF
        const MovementVector direction{cosine, sine, 0.0f};
        const MovementVector point{direction.x * 48.0f + origin.x, direction.y * 48.0f + origin.y,
                                   direction.z * 48.0f + origin.z};
        const MovementVector end{direction.x * 30.0f + origin.x, direction.y * 30.0f + origin.y,
                                 direction.z * 30.0f + origin.z};
        trace_movement_world(engine, make_movement_hull_ray(origin, end, {-1, -1, -8}, {1, 1, 8}),
                             1, trace, trace_flag);
        if (!engine.point_contents_world(point, 1))
            continue;
        total.x += direction.x;
        total.y += direction.y;
        total.z += direction.z;
        ++sample_count;
        if (!blocked(trace) || trace.plane_normal.z >= 0.1f)
            continue;
        total.x += direction.x;
        total.y += direction.y;
        total.z += direction.z;
        ++sample_count;
        const MovementVector normal{negate(trace.plane_normal.x), negate(trace.plane_normal.y),
                                    negate(trace.plane_normal.z)};
        if ((direction.y * normal.y + direction.x * normal.x) + direction.z * normal.z >= 0.5f) {
            normals.x += normal.x;
            normals.y += normal.y;
            normals.z += normal.z;
            ++normal_count;
        }
    }
    if (sample_count <= 1 || !normal_count)
        return false;
    const auto sample_inverse = 1.0f / static_cast<float>(sample_count);
    total.x *= sample_inverse;
    total.y *= sample_inverse;
    total.z *= sample_inverse;
    const auto normal_inverse = 1.0f / static_cast<float>(normal_count);
    normals.x *= normal_inverse;
    normals.y *= normal_inverse;
    const auto inverse = 1.0f / (std::sqrt((normals.x * normals.x + normals.y * normals.y) + 0.0f) +
                                 std::numeric_limits<float>::epsilon());
    normals.x *= inverse;
    normals.y *= inverse;
    auto yaw = planar_movement_yaw(total);
    const auto second_view = engine.view_angles();
    auto difference = second_view.yaw - yaw;
    if (!std::isnan(difference)) {
        if (absolute(difference) > 180.0f) {
            const auto turns = round_movement_step(absolute(difference / 360.0f)) * 360.0f;
            if (difference < 0.0f)
                difference += turns;
            else
                difference -= turns;
        }
        if (difference < 0.0f)
            difference = negate(difference);
        if (difference > 120.0f)
            return false;
    }
    const MovementVector point{total.x * 30.0f + origin.x, total.y * 30.0f + origin.y,
                               total.z * 30.0f + origin.z};
    if (engine.point_contents_world(point, 1) & 1u) {
        MovementVector above = point;
        above.z += 8.0f;
        const MovementVector delta{point.x - above.x, point.y - above.y, point.z - above.z};
        const auto swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
        // The line-ray constructor stores positive zero offsets, unlike the
        // negated hull-center zeros used by the two hull trace constructors.
        const MovementCollisionRay line{above, delta, {0, 0, 0}, {0, 0, 0}, true, swept};
        trace_movement_world(engine, line, 1, trace, trace_flag);
        if (trace.fraction < 1.0f && !trace.start_solid && trace.plane_normal.z > 0.7f) {
            command_yaw = yaw;
            return true;
        }
    }
    const auto correction = clamp(angle_difference(first_view.yaw, yaw) * 1.5f,
                                  negate(settings.maximum_angle), settings.maximum_angle);
    if (absolute(correction) > settings.hull_padding)
        yaw -= correction;
    const auto normal_yaw = planar_movement_yaw(normals);
    difference = angle_difference(yaw, normal_yaw);
    if (difference > settings.maximum_angle)
        yaw = normal_yaw + settings.maximum_angle;
    else if (difference < negate(settings.maximum_angle))
        yaw = normal_yaw - settings.maximum_angle;
    command_yaw = yaw;
    return true;
}
} // namespace skeet::anti_aim
