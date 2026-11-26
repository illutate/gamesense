#include "skeet/legit/acquisition.hpp"
#include "skeet/anti_aim/targets.hpp"
#include "skeet/legit/studio.hpp"
#include <cmath>
#include <cstring>

namespace skeet::legit {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
void acquire(TargetTrack& track, const ProfileState& state,
             std::span<const AcquisitionHitbox> hitboxes, std::span<const Matrix3x4> matrices,
             const Vector& eye, const Vector& forward, float interval, BallisticEngine& engine,
             const BallisticFilter* supplied, const AcquisitionTraceContext* native,
             TriggerTraceBindings bindings, const std::byte* raw_descriptors = nullptr) {
    auto required = state.enabled ? state.aim_hitgroups : 0u;
    if (state.flags & 0x20u)
        required |= state.trigger_hitgroups;
    std::uint32_t visible = 0;
    if (required && (raw_descriptors || hitboxes.size() >= 7)) {
        float best = -1.0f;
        for (std::size_t i = 0; i != 7; ++i) {
            const auto* raw = raw_descriptors ? raw_descriptors + i * 0x44 : nullptr;
            const auto box =
                raw ? AcquisitionHitbox{read<std::int32_t>(raw, 0), read<std::int32_t>(raw, 4),
                                        read<Vector>(raw, 8), read<Vector>(raw, 0x14),
                                        read<float>(raw, 0x30)}
                    : hitboxes[i];
            if (box.bone < 0 || !(box.radius > 0.0f))
                continue;
            const auto group = 1u << (std::uint32_t(box.hitgroup) & 31u);
            if (!(group & required))
                continue;
            const Vector center{(box.maximum[0] + box.minimum[0]) * 0.5f,
                                (box.maximum[1] + box.minimum[1]) * 0.5f,
                                (box.maximum[2] + box.minimum[2]) * 0.5f};
            Vector point;
            for (unsigned row = 0; row != 3; ++row) {
                const auto& m = matrices[i].rows[row];
                point[row] = ((m[1] * center[1] + m[0] * center[0]) + m[2] * center[2]) + m[3];
            }
            if (!(state.flags & 2u) && bindings.through_smoke &&
                bindings.through_smoke(bindings.context, eye, point))
                continue;
            const Vector delta{point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]};
            const auto squared = (delta[1] * delta[1] + delta[0] * delta[0]) + delta[2] * delta[2];
            BallisticTrace trace;
            if (native) {
                anti_aim::TargetTraceFilter owner(native->engine_filter_vtable, native->entities);
                const auto filter = owner.binding();
                engine.trace_ray({{eye, delta, squared != 0.0f},
                                  0x2004001,
                                  BallisticFilterKind::supplied,
                                  &filter,
                                  0,
                                  0},
                                 trace);
            } else
                engine.trace_ray({{eye, delta, squared != 0.0f},
                                  0x2004001,
                                  BallisticFilterKind::supplied,
                                  supplied,
                                  0,
                                  0},
                                 trace);
            if (1.0f > trace.fraction || trace.all_solid || trace.start_solid)
                continue;
            visible |= group;
            if (state.enabled && (state.aim_hitgroups & group)) {
                const auto length =
                    std::sqrt((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2]);
                const auto inverse = 1.0f / (length + 1.1920928955078125e-7f);
                const auto cosine =
                    ((inverse * delta[1]) * forward[1] + (inverse * delta[0]) * forward[0]) +
                    (inverse * delta[2]) * forward[2];
                if (best < cosine) {
                    track.point = point;
                    best = cosine;
                }
            }
        }
    }
    track.visible_hitgroups = visible;
    if (visible)
        track.elapsed += interval;
    else {
        track.elapsed = 0;
        track.point = {};
        track.velocity_valid = 0;
        track.interval = interval;
    }
}
}
void acquire_visible_target(TargetTrack& track, const ProfileState& state,
                            std::span<const AcquisitionHitbox> hitboxes,
                            std::span<const Matrix3x4> matrices, const Vector& eye,
                            const Vector& forward, float interval, BallisticEngine& engine,
                            const BallisticFilter& filter, TriggerTraceBindings bindings) {
    acquire(track, state, hitboxes, matrices, eye, forward, interval, engine, &filter, nullptr,
            bindings);
}
void acquire_visible_target(TargetTrack& track, const ProfileState& state,
                            std::span<const AcquisitionHitbox> hitboxes,
                            std::span<const Matrix3x4> matrices, const Vector& eye,
                            const Vector& forward, float interval, BallisticEngine& engine,
                            const AcquisitionTraceContext& native, TriggerTraceBindings bindings) {
    acquire(track, state, hitboxes, matrices, eye, forward, interval, engine, nullptr, &native,
            bindings);
}
void acquire_visible_target_live(TargetTrack& track, const ProfileState& state, const void* target,
                                 const StudioOffsets& offsets, std::span<const Matrix3x4> matrices,
                                 const Vector& eye, const Vector& forward, float interval,
                                 BallisticEngine& engine, const AcquisitionTraceContext& native,
                                 TriggerTraceBindings bindings) {
    auto required = state.enabled ? state.aim_hitgroups : 0u;
    if (state.flags & 0x20u)
        required |= state.trigger_hitgroups;
    const std::byte* descriptors = nullptr;
    if (required) {
        const auto set = load_target_hitbox_set(target, offsets);
        if (set.studio && set.count >= 7)
            descriptors = set.descriptors;
    }
    acquire(track, state, {}, matrices, eye, forward, interval, engine, nullptr, &native, bindings,
            descriptors);
}
} // namespace skeet::legit
