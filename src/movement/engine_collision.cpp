#include "skeet/movement/engine_collision.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/trace.hpp"
#include <stdexcept>

namespace skeet {
namespace {
engine::TraceRay native_ray(const MovementCollisionRay& source) noexcept {
    engine::TraceRay ray;
    ray.start = {source.start.x, source.start.y, source.start.z, 0.0f};
    ray.delta = {source.delta.x, source.delta.y, source.delta.z, 0.0f};
    ray.start_offset = {source.start_offset.x, source.start_offset.y, source.start_offset.z, 0.0f};
    ray.extents = {source.extents.x, source.extents.y, source.extents.z, 0.0f};
    ray.is_ray = source.is_ray;
    ray.swept = source.swept;
    return ray;
}

// Native4346EDC0 methods read434758B4 at every invocation. Keep the current
// table cell, rather than copying its entries before the engine TraceRay call.
struct LiveSkipFilter {
    engine::TraceSkipFilterPrefix prefix;
    const MovementEngineCollisionBindings* bindings;
#if defined(_M_IX86) || defined(__i386__)
    static bool __thiscall should_hit(void* receiver, void* entity, std::uint32_t mask) {
#else
    static bool should_hit(void* receiver, void* entity, std::uint32_t mask) {
#endif
        const auto& self = *static_cast<LiveSkipFilter*>(receiver);
        const auto table = self.bindings->skip_filter_vtable;
        if (!table)
            throw std::logic_error("Required movement trace filter table is not bound");
        return reinterpret_cast<engine::MemberFunction<bool, void*, std::uint32_t>>(table[0])(
            receiver, entity, mask);
    }
#if defined(_M_IX86) || defined(__i386__)
    static std::int32_t __thiscall trace_type(void* receiver){
#else
    static std::int32_t trace_type(void* receiver) {
#endif
        const auto& self = *static_cast<LiveSkipFilter*>(receiver);
    const auto table = self.bindings->skip_filter_vtable;
    if (!table)
        throw std::logic_error("Required movement trace filter table is not bound");
    return reinterpret_cast<engine::MemberFunction<std::int32_t>>(table[1])(receiver);
} LiveSkipFilter(MovementCollisionFilter filter, const MovementEngineCollisionBindings& source)
    : prefix{table(), reinterpret_cast<void*>(std::uintptr_t(filter.player)),
             filter.collision_group, filter.extra},
      bindings(&source) {
}

private:
static void* const* table() noexcept {
    static void* const methods[]{reinterpret_cast<void*>(should_hit),
                                 reinterpret_cast<void*>(trace_type)};
    return methods;
}
};
static_assert(offsetof(LiveSkipFilter, prefix) == 0);
}

void EngineMovementCollision::trace_ray(const MovementCollisionRay& source, std::uint32_t mask,
                                        MovementCollisionFilter filter,
                                        MovementCollisionTrace& result) {
    auto ray = native_ray(source);
    switch (filter.kind) {
    case MovementCollisionFilterKind::reject_entities: {
        engine::TraceRejectEntitiesFilter native_filter;
        engine::invoke<void>(bindings_.engine_trace, 5, &ray, mask, &native_filter, &result);
        return;
    }
    case MovementCollisionFilterKind::skip_player: {
        LiveSkipFilter native_filter(filter, bindings_);
        engine::invoke<void>(bindings_.engine_trace, 5, &ray, mask, &native_filter, &result);
        return;
    }
    case MovementCollisionFilterKind::world_only: {
        engine::TraceWorldOnlyFilter native_filter;
        engine::invoke<void>(bindings_.engine_trace, 5, &ray, mask, &native_filter, &result);
        return;
    }
    }
    throw std::logic_error("Invalid movement collision filter kind");
}

float EngineMovementCollision::surface_friction(std::int16_t property) {
    // Every recovered categorization initializes its friction output to one
    // before tracing/calling SurfacePhysics; preserve that initial storage.
    float friction = 1.0f;
    engine::invoke<void>(bindings_.surface_properties, 4, std::int32_t(property),
                         static_cast<float*>(nullptr), static_cast<float*>(nullptr), &friction,
                         static_cast<float*>(nullptr));
    return friction;
}

} //namespace skeet
