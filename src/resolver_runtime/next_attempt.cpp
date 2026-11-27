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

namespace {
template <class T> T field(const void* object, std::ptrdiff_t at) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + at, sizeof result);
    return result;
}

std::uint8_t pick_non_intersecting_increment(
    std::uint8_t previous, std::uint8_t initial, const RageRay& ray,
    const std::array<const void*, 19>& hitboxes, const lagcomp::LagRecord& record,
    const RageBoxClipper& clipper) {
    constexpr std::size_t order[3] = {1, 2, 0};
    const auto max_checks = static_cast<std::uint8_t>(3u - (initial & 1u));
    for (std::uint8_t offset = 0; offset < max_checks; ++offset) {
        const auto attempt = static_cast<std::uint8_t>(previous + initial + offset);
        const auto index = order[attempt % 3];
        if (!intersects_hypothesis(ray, hitboxes, record.alternatives[index].matrices, clipper))
            return static_cast<std::uint8_t>(initial + offset);
    }
    return 1;
}

RageRay build_shot_ray(const RageVector& start, const RageVector& direction,
                       float distance) noexcept {
    RageRay ray{start, {}};
    for (std::size_t axis = 0; axis != 3; ++axis) {
        const float endpoint = direction[axis] * distance + start[axis];
        ray.delta[axis] = endpoint - start[axis];
    }
    return ray;
}

struct TypedAttemptAdapter {
    const AttemptEngine& engine;
    float current_time;
    AttemptTarget* target = nullptr;

    bool lookup(std::uint32_t handle) {
        target = engine.lookup_handle(engine.context, handle);
        return target != nullptr;
    }
    float now() const noexcept { return current_time; }
    void apply_pose_adjustment(float elapsed) {
        if (!target || target->animation_adjustment == nullptr || elapsed < 0.0f || elapsed >= 0.5f)
            return;
        float adjustment = 0.0f;
        if (target->studio_model != nullptr)
            adjustment = engine.convert_pose_parameter(engine.context, target->studio_model, 11,
                                                       target->body_pose_parameter);
        *target->animation_adjustment += adjustment;
        *target->animation_adjustment += adjustment;
    }
    bool ready_for_geometry() const noexcept {
        return target != nullptr && !target->hidden && target->animation_adjustment != nullptr &&
               target->studio_model != nullptr && target->hitbox_set && target->hitbox_count != 0;
    }
    std::array<const void*, 19> hitboxes() const noexcept { return target->hitboxes; }
    const RageBoxClipper& clipper() const noexcept { return engine.clipper; }
};

struct RawAttemptAdapter {
    const RawAttemptEngine& source;
    void* target = nullptr;
    std::byte* animation = nullptr;
    const std::byte* set = nullptr;

    bool lookup(std::uint32_t handle) {
        target = engine::invoke<void*>(source.entity_list, 3, handle);
        if (!target)
            return false;
        animation = field<std::byte*>(target, source.animation_state);
        return true;
    }
    float now() const noexcept { return *source.current_time; }
    void apply_pose_adjustment(float elapsed) {
        if (!animation || elapsed < 0.0f || elapsed >= 0.5f)
            return;
        float adjustment = 0.0f;
        const auto* wrapper = field<const void*>(target, source.studio_wrapper);
        if (wrapper && field<const void*>(wrapper, 0))
            adjustment =
                source.pose.convert(wrapper, 11, field<float>(target, source.pose_parameters + 44));
        auto value = field<float>(animation, 0x80);
        value += adjustment;
        value += adjustment;
        std::memcpy(animation + 0x80, &value, sizeof value);
    }
    bool ready_for_geometry() {
        if (!target || field<std::uint8_t>(target, source.hidden) || !animation)
            return false;
        const auto* wrapper = field<const std::byte*>(target, source.studio_wrapper);
        const auto* header = wrapper ? field<const std::byte*>(wrapper, 0) : nullptr;
        if (!header)
            return false;
        set = header + field<std::int32_t>(header, 0xB0) +
              std::ptrdiff_t(field<std::int32_t>(target, source.hitbox_set)) * 12;
        return field<std::uint32_t>(set, 4) != 0;
    }
    std::array<const void*, 19> hitboxes() const noexcept {
        std::array<const void*, 19> descriptors;
        const auto* first = set + field<std::int32_t>(set, 8);
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            descriptors[i] = first + i * 0x44;
        return descriptors;
    }
    const RageBoxClipper& clipper() const noexcept { return source.clipper; }
};

template <class Adapter>
std::uint8_t choose_next_attempt_impl(const AttemptOwner& owner, const AttemptShot& shot,
                                      bool geometry_enabled, const RageVector* start,
                                      const RageVector* direction, Adapter adapter) {
    const auto mode = shot.record.header.resolver_index;
    const auto previous = shot.record.header.resolver_mode;
    auto increment = static_cast<std::uint8_t>((~shot.shot_flags) & 1U);
    if (mode == 0) {
        if (adapter.lookup(shot.target_handle))
            adapter.apply_pose_adjustment(adapter.now() - owner.previous_pose_time);
    } else if (mode < 3) {
        if (geometry_enabled && start != nullptr && direction != nullptr &&
            (shot.shot_flags & 0x28U) == 0 && (shot.record.header.flags & 0x80U) != 0 &&
            (shot.prefix_flags & 0x1eU) == 0 && adapter.lookup(shot.target_handle) &&
            adapter.ready_for_geometry()) {
            const auto ray = build_shot_ray(*start, *direction, shot.distance);
            increment = pick_non_intersecting_increment(previous, increment, ray,
                                                        adapter.hitboxes(), shot.record,
                                                        adapter.clipper());
        }
    } else if (mode == 3 && previous % 3 == 1 &&
               std::fabs(adapter.now() - owner.previous_selection_time) > 10.0f) {
        increment = 2;
    }
    return static_cast<std::uint8_t>(previous + increment);
}
} // namespace

std::uint8_t choose_next_attempt(const AttemptOwner& owner, const AttemptShot& shot,
                                 float current_time, bool geometry_enabled, const RageVector* start,
                                 const RageVector* direction, const AttemptEngine& engine) {
    return choose_next_attempt_impl(owner, shot, geometry_enabled, start, direction,
                                    TypedAttemptAdapter{engine, current_time});
}
std::uint8_t choose_next_attempt(const AttemptOwner& owner, const AttemptShot& shot, float,
                                 bool geometry_enabled, const RageVector* start,
                                 const RageVector* direction, const RawAttemptEngine& source) {
    return choose_next_attempt_impl(owner, shot, geometry_enabled, start, direction,
                                    RawAttemptAdapter{source});
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
