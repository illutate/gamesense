#include "skeet/resolver_runtime/next_attempt.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cmath>
#include <cstring>

namespace skeet::resolver_runtime {

bool intersects_hypothesis(const RageRay& ray, const std::array<const void*, 19>& descriptors,
                           const HitboxMatrices& matrices, const RageBoxClipper& engine) {
    RageTrace trace;
    for (std::size_t index = 0; index != 19; ++index) {
        RageScanHitbox hitbox{};
        hitbox.engine_hitbox = descriptors[index];
        hitbox.transform = matrices[index];
        if (engine.clip(engine.context, ray, hitbox, trace) >= 0)
            return true;
    }
    return false;
}

std::uint8_t choose_next_attempt(const AttemptOwner& owner, const AttemptShot& shot,
                                 float current_time, bool geometry_enabled, const RageVector* start,
                                 const RageVector* direction, const AttemptEngine& engine) {
    const auto mode = shot.record.header.resolver_index;
    const auto previous = shot.record.header.resolver_mode;
    std::uint8_t increment = static_cast<std::uint8_t>((~shot.shot_flags) & 1U);
    if (mode == 0) {
        auto* target = engine.lookup_handle(engine.context, shot.target_handle);
        if (target != nullptr && target->animation_adjustment != nullptr) {
            const float elapsed = current_time - owner.previous_pose_time;
            if (elapsed >= 0.0f && elapsed < 0.5f) {
                float adjustment = 0.0f;
                if (target->studio_model != nullptr)
                    adjustment = engine.convert_pose_parameter(engine.context, target->studio_model,
                                                               11, target->body_pose_parameter);
                *target->animation_adjustment += adjustment;
                *target->animation_adjustment += adjustment;
            }
        }
    } else if (mode < 3) {
        if (geometry_enabled && start != nullptr && direction != nullptr &&
            (shot.shot_flags & 0x28U) == 0) {
            auto* target = engine.lookup_handle(engine.context, shot.target_handle);
            if (target != nullptr && !target->hidden && target->animation_adjustment != nullptr &&
                (shot.record.header.flags & 0x80U) == 0 && (shot.prefix_flags & 0x1eU) == 0 &&
                target->studio_model != nullptr && target->hitbox_set &&
                target->hitbox_count != 0) {
                RageRay ray{*start, {}};
                for (std::size_t axis = 0; axis != 3; ++axis) {
                    const float endpoint = (*direction)[axis] * shot.distance + (*start)[axis];
                    ray.delta[axis] = endpoint - (*start)[axis];
                }
                constexpr std::size_t order[3] = {1, 2, 0};
                const auto attempt = static_cast<std::uint8_t>(previous + increment);
                const auto index = order[attempt % 3];
                if (intersects_hypothesis(ray, target->hitboxes,
                                          shot.record.alternatives[index].matrices, engine.clipper))
                    ++increment;
            }
        }
    } else if (mode == 3 && previous % 3 == 1 &&
               std::fabs(current_time - owner.previous_selection_time) > 10.0f)
        increment = 2;
    return static_cast<std::uint8_t>(previous + increment);
}

namespace {
template <class T> T field(const void* object, std::ptrdiff_t at) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + at, sizeof result);
    return result;
}
}
std::uint8_t choose_next_attempt(const AttemptOwner& owner, const AttemptShot& shot, float,
                                 bool geometry_enabled, const RageVector* start,
                                 const RageVector* direction, const RawAttemptEngine& source) {
    const auto mode = shot.record.header.resolver_index;
    const auto previous = shot.record.header.resolver_mode;
    auto increment = static_cast<std::uint8_t>((~shot.shot_flags) & 1u);
    if (mode == 0) {
        auto* target = engine::invoke<void*>(source.entity_list, 3, shot.target_handle);
        if (target)
            if (auto* animation = field<std::byte*>(target, source.animation_state)) {
                const float elapsed = *source.current_time - owner.previous_pose_time;
                if (elapsed >= 0.0f && elapsed < 0.5f) {
                    float adjustment = 0.0f;
                    const auto* wrapper = field<const void*>(target, source.studio_wrapper);
                    if (wrapper && field<const void*>(wrapper, 0))
                        adjustment = source.pose.convert(
                            wrapper, 11, field<float>(target, source.pose_parameters + 44));
                    auto value = field<float>(animation, 0x80);
                    value += adjustment;
                    value += adjustment;
                    std::memcpy(animation + 0x80, &value, sizeof value);
                }
            }
    } else if (mode < 3) {
        if (geometry_enabled && start && direction && !(shot.shot_flags & 0x28u)) {
            auto* target = engine::invoke<void*>(source.entity_list, 3, shot.target_handle);
            if (target && !field<std::uint8_t>(target, source.hidden) &&
                field<const void*>(target, source.animation_state) &&
                !(shot.record.header.flags & 0x80u) && !(shot.prefix_flags & 0x1Eu)) {
                const auto* wrapper = field<const std::byte*>(target, source.studio_wrapper);
                const auto* header = wrapper ? field<const std::byte*>(wrapper, 0) : nullptr;
                if (header) {
                    const auto* set =
                        header + field<std::int32_t>(header, 0xB0) +
                        std::ptrdiff_t(field<std::int32_t>(target, source.hitbox_set)) * 12;
                    if (field<std::uint32_t>(set, 4)) {
                        RageRay ray{*start, {}};
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            const auto endpoint =
                                (*direction)[axis] * shot.distance + (*start)[axis];
                            ray.delta[axis] = endpoint - (*start)[axis];
                        }
                        constexpr std::size_t order[]{1, 2, 0};
                        const auto index =
                            order[static_cast<std::uint8_t>(previous + increment) % 3];
                        std::array<const void*, 19> descriptors;
                        const auto* first = set + field<std::int32_t>(set, 8);
                        for (std::size_t i = 0; i < descriptors.size(); ++i)
                            descriptors[i] = first + i * 0x44;
                        if (intersects_hypothesis(ray, descriptors,
                                                  shot.record.alternatives[index].matrices,
                                                  source.clipper))
                            ++increment;
                    }
                }
            }
        }
    } else if (mode == 3 && previous % 3 == 1 &&
               std::fabs(*source.current_time - owner.previous_selection_time) > 10.0f)
        increment = 2;
    return static_cast<std::uint8_t>(previous + increment);
}
std::uint8_t choose_next_attempt(const AttemptOwner& owner, const AttemptShot& shot,
                                 float current_time, bool geometry_enabled, const RageVector* start,
                                 const RageVector* direction, const AttemptEngineBinding& source) {
    return std::visit(
        [&](auto selected) {
            return choose_next_attempt(owner, shot, current_time, geometry_enabled, start,
                                       direction, selected.get());
        },
        source.source());
}

} // namespace skeet::resolver_runtime
