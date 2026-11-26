#include "skeet/anti_aim/body_freestanding.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace skeet::anti_aim {
namespace {
float neg(float x) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(x) ^ 0x80000000u);
}
float length(RageVector x) {
    return std::sqrt((x[0] * x[0] + x[1] * x[1]) + x[2] * x[2]);
}
RageVector subtract(RageVector a, RageVector b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
RageVector normalized(RageVector a) {
    const float inverse = 1.0f / (length(a) + std::numeric_limits<float>::epsilon());
    return {inverse * a[0], inverse * a[1], inverse * a[2]};
}
RageVector angles(RageVector direction, const BodyFreestandingMath& math) {
    if (direction[0] == 0.0f && direction[1] == 0.0f)
        return {direction[2] > 0.0f ? 270.0f : 90.0f, 0.0f, 0.0f};
    constexpr float degrees = 57.2957763671875f;
    const float yaw =
        static_cast<float>(math.atan2(double(direction[1]), double(direction[0]))) * degrees;
    const float horizontal = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    const float pitch =
        static_cast<float>(math.atan2(double(neg(direction[2])), double(horizontal))) * degrees;
    return {pitch, yaw, 0.0f};
}
RageVector transform(const Matrix3x4& matrix, RageVector point) {
    RageVector result;
    for (unsigned i = 0; i != 3; ++i)
        result[i] = ((matrix.rows[i][0] * point[0] + matrix.rows[i][1] * point[1]) +
                     matrix.rows[i][2] * point[2]) +
                    matrix.rows[i][3];
    return result;
}
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool build(BodyFreestandingBones& bones, const animation::BoneRequest& request,
           std::span<Matrix3x4> output, const RageTrigonometry& trig) {
    return animation::build_bone_matrices(*bones.state, *bones.model, request, output,
                                          *bones.engine, trig);
}
bool build(LiveBodyFreestandingBones& bones, const animation::BoneRequest& request,
           std::span<Matrix3x4> output, const RageTrigonometry&) {
    return bones.runtime.build_bone_matrices(bones.player, request, output);
}
std::uint32_t hitbox_bone(const BodyFreestandingBones& bones, unsigned index) noexcept {
    return bones.model->hitbox_bones[index];
}
std::uint32_t hitbox_bone(const LiveBodyFreestandingBones& bones, unsigned index) noexcept {
    return bones.hitbox_bones[index];
}
}
LiveBodyFreestandingModel capture_body_freestanding_model(const animation::BoneRuntime& runtime,
                                                          const void* player) {
    LiveBodyFreestandingModel result{};
    const auto* wrapper = read<const void*>(player, runtime.fields.studio_wrapper_32e4daf1);
    if (!wrapper)
        return result;
    const auto* header = read<const std::byte*>(wrapper, 0);
    if (!header)
        return result;
    const auto relative =
        std::uint32_t(read<std::int32_t>(player, runtime.fields.hitbox_set_c2b6fb10)) * 12u +
        std::uint32_t(read<std::int32_t>(header, 0xB0));
    const auto* set = header + std::bit_cast<std::int32_t>(relative);
    result.hitbox_set_present = true;
    result.hitbox_count = std::min(19, read<std::int32_t>(set, 4));
    if (result.hitbox_count <= 0)
        return result;
    const auto* boxes = set + read<std::int32_t>(set, 8);
    for (std::int32_t i = 0; i < result.hitbox_count; ++i) {
        const auto* box = boxes + std::ptrdiff_t(i) * 0x44;
        result.hitbox_bones[std::size_t(i)] = read<std::uint32_t>(box, 0);
        result.hitboxes[std::size_t(i)] = {read<std::int32_t>(box, 4),  read<RageVector>(box, 8),
                                           read<RageVector>(box, 0x14), read<RageVector>(box, 0x24),
                                           read<float>(box, 0x30),      box};
    }
    return result;
}
LiveBodyFreestandingBones bind_body_freestanding_bones(const animation::BoneRuntime& runtime,
                                                       void* player,
                                                       const LiveBodyFreestandingModel& model,
                                                       std::uint32_t eye_angles,
                                                       std::uint32_t pose_parameters) noexcept {
    auto* const bytes = static_cast<std::byte*>(player);
    auto* const pitch = reinterpret_cast<float*>(bytes + eye_angles);
    const auto count = std::size_t(std::max(0, model.hitbox_count));
    return {player,
            runtime,
            pitch,
            pitch + 1,
            reinterpret_cast<float*>(bytes + pose_parameters + 0x2C),
            model.hitbox_set_present,
            model.hitbox_count,
            std::int32_t(model.hitbox_bones[0]),
            model.hitboxes[0].minimum,
            model.hitboxes[0].maximum,
            {model.hitboxes.data(), count},
            {model.hitbox_bones.data(), count}};
}
namespace {
struct DecodedModelProjection {};
DecodedModelProjection capture_model(const BodyFreestandingBones&) noexcept {
    return {};
}
BodyFreestandingBones bind_model(const BodyFreestandingBones& bones,
                                 DecodedModelProjection) noexcept {
    return bones;
}
LiveBodyFreestandingModel capture_model(const LiveBodyFreestandingSource& source) {
    return capture_body_freestanding_model(source.runtime, source.player);
}
LiveBodyFreestandingBones bind_model(const LiveBodyFreestandingSource& source,
                                     const LiveBodyFreestandingModel& model) noexcept {
    return bind_body_freestanding_bones(source.runtime, source.player, model, source.eye_angles,
                                        source.pose_parameters);
}
}
template <class Bones>
bool solve_animation_body(const RageVector& frame_origin, const BodyFreestandingContext& context,
                          Bones& source, float maximum_yaw, float& result,
                          const BodyFreestandingMath& math, BodyFreestandingWorkspace& work) {
    if (context.target_handle == -1 ||
        !context.entities.by_handle(context.entities.object, context.target_handle))
        return false;
    const auto look = angles(normalized(subtract(context.shot_position, frame_origin)), math);
    const auto model = capture_model(source);
    auto bones = bind_model(source, model);
    if (!bones.hitbox_set_present || bones.hitbox_count == 0)
        return false;
    const float saved_pitch = *bones.eye_pitch, saved_yaw = *bones.eye_yaw,
                saved_pose = *bones.pose_parameter_11;
    *bones.eye_pitch = look[0];
    *bones.eye_yaw = look[1];
    const std::array<float, 3> offsets{maximum_yaw, 0.0f, neg(maximum_yaw)};
    for (unsigned i = 0; i != 3; ++i) {
        float pose = (offsets[i] - (-60.0f)) / 120.0f + 0.0f;
        pose = pose > 0.0f ? pose : 0.0f;
        pose = pose < 1.0f ? pose : 1.0f;
        *bones.pose_parameter_11 = pose;
        const auto origin = *engine::invoke<const RageVector*>(bones.player, 10);
        const animation::BoneRequest request{
            {0.0f, look[1] - offsets[i], 0.0f}, origin, 0.0f, 0x300, -1, 0, true};
        if (build(bones, request, work.matrices, math.trigonometry)) {
            const auto& matrix = work.matrices[unsigned(bones.first_hitbox_bone)];
            const auto minimum = transform(matrix, bones.first_hitbox_minimum);
            const auto maximum = transform(matrix, bones.first_hitbox_maximum);
            for (unsigned axis = 0; axis != 3; ++axis)
                work.centers[i][axis] = (minimum[axis] + maximum[axis]) * 0.5f;
        }
    }
    auto target = context.target_origin;
    target[2] += 64.0f;
    const auto forward = normalized(subtract(work.centers[1], target));
    RageVector side;
    if (forward[0] == 0.0f && forward[1] == 0.0f)
        side = {0.0f, -1.0f, 0.0f};
    else {
        const float common = forward[2] * 0.0f;
        side = normalized(
            {forward[1] - common, common - forward[0], forward[0] * 0.0f - forward[1] * 0.0f});
    }
    const auto displacement = [&](const RageVector& point) {
        const auto delta = subtract(point, work.centers[1]);
        const float projection = (delta[1] * side[1] + delta[0] * side[0]) + delta[2] * side[2];
        RageVector projected;
        for (unsigned i = 0; i != 3; ++i)
            projected[i] = (side[i] * projection + work.centers[1][i]) - work.centers[1][i];
        return length(projected);
    };
    result = displacement(work.centers[0]) > displacement(work.centers[2]) ? maximum_yaw
                                                                           : neg(maximum_yaw);
    *bones.pose_parameter_11 = saved_pose;
    *bones.eye_yaw = saved_yaw;
    *bones.eye_pitch = saved_pitch;
    return true;
}
template <class Bones>
bool occludes_head(const RageRay& ray, const Bones& bones, std::span<const Matrix3x4> matrices,
                   const RageBoxClipper& clipper) {
    const auto clip = [&](unsigned index, RageTrace& trace) {
        const auto& h = bones.hitboxes[index];
        const RageScanHitbox hitbox{
            h.hitgroup, {{}, {}, h.radius}, h.engine_hitbox, matrices[hitbox_bone(bones, index)]};
        return clipper.clip(clipper.context, ray, hitbox, trace);
    };
    RageTrace trace;
    if (clip(0, trace) < 0)
        return false;
    const float head_fraction = trace.fraction;
    for (unsigned i = 0; i != bones.hitboxes.size(); ++i) {
        const auto group = bones.hitboxes[i].hitgroup;
        if (group != 2 && group != 3)
            continue;
        trace.fraction = 1.0f;
        if (clip(i, trace) >= 0 && head_fraction > trace.fraction)
            return true;
    }
    return false;
}
template <class Bones>
bool solve_covered_body(const BodyFreestandingSettings& settings, const RageVector& frame_origin,
                        const BodyFreestandingContext& context, Bones& source, float command_pitch,
                        float base_yaw, float maximum_yaw, float& result,
                        const BodyFreestandingMath& math, const RageBoxClipper& clipper,
                        BodyFreestandingWorkspace& work) {
    if (context.target_handle == -1 ||
        !context.entities.by_handle(context.entities.object, context.target_handle))
        return false;
    if (settings.yaw_mode_2c != 1)
        return false;
    const float dx = frame_origin[0] - context.target_origin[0],
                dy = frame_origin[1] - context.target_origin[1];
    float yaw = 0.0f;
    if (dy != 0.0f || dx != 0.0f)
        yaw = static_cast<float>(math.atan2(double(dy), double(dx))) * 57.2957763671875f;
    float difference = normalize_movement_angle(yaw - base_yaw);
    if (difference < 0.0f)
        difference = neg(difference);
    if (difference > settings.maximum_angle_148)
        return false;
    const auto model = capture_model(source);
    auto bones = bind_model(source, model);
    if (!bones.hitbox_set_present || bones.hitbox_count == 0)
        return false;
    const float saved_pitch = *bones.eye_pitch, saved_yaw = *bones.eye_yaw,
                saved_pose = *bones.pose_parameter_11;
    *bones.eye_pitch = command_pitch;
    *bones.eye_yaw = base_yaw;
    const std::array<float, 3> offsets{maximum_yaw, 0.0f, neg(maximum_yaw)};
    auto target = context.target_origin;
    target[2] += 64.0f;
    unsigned maximum = 0, minimum = 0xFFFFFFFFu;
    for (unsigned hypothesis = 0; hypothesis != 3; ++hypothesis) {
        float pose = (offsets[hypothesis] - (-60.0f)) / 120.0f + 0.0f;
        pose = pose > 0.0f ? pose : 0.0f;
        pose = pose < 1.0f ? pose : 1.0f;
        *bones.pose_parameter_11 = pose;
        const auto origin = *engine::invoke<const RageVector*>(bones.player, 10);
        const animation::BoneRequest request{
            {0.0f, base_yaw - offsets[hypothesis], 0.0f}, origin, 0.0f, 0x300, -1, 0, true};
        if (!build(bones, request, work.matrices, math.trigonometry))
            continue;
        const auto& matrix = work.matrices[unsigned(bones.first_hitbox_bone)];
        const auto low = transform(matrix, bones.first_hitbox_minimum),
                   high = transform(matrix, bones.first_hitbox_maximum);
        RageVector center;
        for (unsigned i = 0; i != 3; ++i)
            center[i] = (high[i] + low[i]) * 0.5f;
        const auto forward = normalized(subtract(center, target));
        RageVector side, up;
        if (forward[0] == 0.0f && forward[1] == 0.0f) {
            side = {0.0f, -1.0f, 0.0f};
            up = {neg(forward[2]), 0.0f, 0.0f};
        } else {
            const float common = forward[2] * 0.0f;
            side = normalized(
                {forward[1] - common, common - forward[0], forward[0] * 0.0f - forward[1] * 0.0f});
            up = normalized({side[1] * forward[2] - side[2] * forward[1],
                             side[2] * forward[0] - side[0] * forward[2],
                             side[0] * forward[1] - side[1] * forward[0]});
        }
        constexpr std::array<float, 5> side_weights{-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        constexpr std::array<float, 5> up_weights{0.25f, 0.75f, 1.0f, 0.75f, 0.25f};
        const float radius = bones.hitboxes[0].radius * 0.7f;
        const auto axis = subtract(high, low);
        unsigned coverage = 0;
        for (unsigned sample = 0; sample != 5; ++sample) {
            RageVector direction;
            for (unsigned i = 0; i != 3; ++i)
                direction[i] = side[i] * side_weights[sample] + up[i] * up_weights[sample];
            direction = normalized(direction);
            const float dot =
                (axis[1] * direction[1] + axis[0] * direction[0]) + axis[2] * direction[2];
            const auto& endpoint = dot < 0.0f ? low : high;
            const float distance = radius / length(direction);
            RageVector point;
            for (unsigned i = 0; i != 3; ++i)
                point[i] = distance * direction[i] + endpoint[i];
            const auto to_point = subtract(point, target);
            const float extension =
                40.0f / (length(to_point) + std::numeric_limits<float>::epsilon());
            RageRay ray{target, {}};
            for (unsigned i = 0; i != 3; ++i)
                ray.delta[i] = (extension * to_point[i] + point[i]) - target[i];
            if (occludes_head(ray, bones, work.matrices, clipper))
                ++coverage;
        }
        if (coverage > maximum) {
            result = offsets[hypothesis];
            maximum = coverage;
        }
        if (coverage < minimum)
            minimum = coverage;
    }
    *bones.pose_parameter_11 = saved_pose;
    *bones.eye_yaw = saved_yaw;
    *bones.eye_pitch = saved_pitch;
    return minimum < maximum;
}
bool solve_animation_body_freestanding(const RageVector& origin,
                                       const BodyFreestandingContext& context,
                                       BodyFreestandingBones& bones, float yaw, float& result,
                                       const BodyFreestandingMath& math,
                                       BodyFreestandingWorkspace& work) {
    return solve_animation_body(origin, context, bones, yaw, result, math, work);
}
bool solve_animation_body_freestanding(const RageVector& origin,
                                       const BodyFreestandingContext& context,
                                       LiveBodyFreestandingSource& bones, float yaw, float& result,
                                       const BodyFreestandingMath& math,
                                       BodyFreestandingWorkspace& work) {
    return solve_animation_body(origin, context, bones, yaw, result, math, work);
}
bool body_occludes_head(const RageRay& ray, const BodyFreestandingBones& bones,
                        std::span<const Matrix3x4> matrices, const RageBoxClipper& clipper) {
    return occludes_head(ray, bones, matrices, clipper);
}
bool body_occludes_head(const RageRay& ray, const LiveBodyFreestandingBones& bones,
                        std::span<const Matrix3x4> matrices, const RageBoxClipper& clipper) {
    return occludes_head(ray, bones, matrices, clipper);
}
bool solve_covered_body_freestanding(const BodyFreestandingSettings& settings,
                                     const RageVector& origin,
                                     const BodyFreestandingContext& context,
                                     BodyFreestandingBones& bones, float pitch, float yaw,
                                     float maximum, float& result, const BodyFreestandingMath& math,
                                     const RageBoxClipper& clipper,
                                     BodyFreestandingWorkspace& work) {
    return solve_covered_body(settings, origin, context, bones, pitch, yaw, maximum, result, math,
                              clipper, work);
}
bool solve_covered_body_freestanding(const BodyFreestandingSettings& settings,
                                     const RageVector& origin,
                                     const BodyFreestandingContext& context,
                                     LiveBodyFreestandingSource& bones, float pitch, float yaw,
                                     float maximum, float& result, const BodyFreestandingMath& math,
                                     const RageBoxClipper& clipper,
                                     BodyFreestandingWorkspace& work) {
    return solve_covered_body(settings, origin, context, bones, pitch, yaw, maximum, result, math,
                              clipper, work);
}
} // namespace skeet::anti_aim
