#include "skeet/esp/grenade_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/collision.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>
#include <xmmintrin.h>

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::uint32_t identity(const void* value) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
}
void* pointer(std::uint32_t value) noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(value));
}
engine::TraceRay native_ray(const GrenadeRay& source) noexcept {
    engine::TraceRay ray;
    std::memcpy(ray.start.data(), &source.start, sizeof source.start);
    std::memcpy(ray.delta.data(), &source.delta, sizeof source.delta);
    std::memcpy(ray.extents.data(), &source.extents, sizeof source.extents);
    if (!source.is_ray) {
        // Native symmetric hull construction adds the zero center offset to
        // start, then negates that offset. Preserve the resulting signed zeros.
        for (std::size_t i = 0; i < 3; ++i)
            ray.start[i] = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(ray.start[i]), _mm_setzero_ps()));
        constexpr auto negative_zero = std::bit_cast<float>(0x80000000u);
        ray.start_offset = {negative_zero, negative_zero, negative_zero, 0};
    }
    ray.is_ray = source.is_ray;
    ray.swept = source.swept;
    return ray;
}
#if defined(_M_IX86) || defined(__i386__)
#define GRENADE_MEMBER __thiscall
#else
#define GRENADE_MEMBER
#endif

void* const* required_table(void* const* table) {
    if (!table)
        throw std::logic_error("Required grenade trace filter table is not bound");
    return table;
}
void initialize_trace(MovementCollisionTrace& result) {
    // Native433B7469 and the repeated HE trace records initialize these fields
    // before the engine fills the complete0x54-byte trace. Fraction is one.
    result.fraction = 1.0f;
    result.all_solid = 0;
    result.start_solid = 0;
    result.hitgroup = -1;
    result.entity = 0;
    result.hitbox = -1;
}

struct ProjectileFilter {
    engine::TraceSkipFilterPrefix prefix;
    std::uint32_t field_10 = 0;
    std::int16_t kind;
    std::uint16_t bounces;
    const GrenadeEngineBindings* bindings;
    static bool GRENADE_MEMBER should_hit(void* object, void* entity, std::uint32_t mask) {
        const auto& self = *static_cast<ProjectileFilter*>(object);
        const auto table = required_table(self.bindings->entity_filter_vtable);
        return reinterpret_cast<engine::MemberFunction<bool, void*, std::uint32_t>>(table[0])(
            object, entity, mask);
    }
    static std::int32_t GRENADE_MEMBER trace_type(void* object) {
        const auto& self = *static_cast<ProjectileFilter*>(object);
        const auto table = required_table(self.bindings->entity_filter_vtable);
        return reinterpret_cast<engine::MemberFunction<std::int32_t>>(table[1])(object);
    }
    static void GRENADE_MEMBER set_entity(void* object, void* entity) {
        const auto& self = *static_cast<ProjectileFilter*>(object);
        const auto table = required_table(self.bindings->entity_filter_vtable);
        reinterpret_cast<engine::MemberFunction<void, void*>>(table[2])(object, entity);
    }
    static void GRENADE_MEMBER set_group(void* object, std::int32_t group) {
        const auto& self = *static_cast<ProjectileFilter*>(object);
        const auto table = required_table(self.bindings->entity_filter_vtable);
        reinterpret_cast<engine::MemberFunction<void, std::int32_t>>(table[3])(object, group);
    }
    static void GRENADE_MEMBER set_extra(void* object, std::uint32_t extra) {
        const auto& self = *static_cast<ProjectileFilter*>(object);
        const auto table = required_table(self.bindings->entity_filter_vtable);
        reinterpret_cast<engine::MemberFunction<void, std::uint32_t>>(table[4])(object, extra);
    }
    static void* const* table() {
        static void* const methods[]{
            reinterpret_cast<void*>(should_hit), reinterpret_cast<void*>(trace_type),
            reinterpret_cast<void*>(set_entity), reinterpret_cast<void*>(set_group),
            reinterpret_cast<void*>(set_extra)};
        return methods;
    }
    ProjectileFilter(const GrenadeTraceFilter& source, const GrenadeEngineBindings& actual)
        : prefix{table(), pointer(source.ignored_entity), source.collision_group, 0},
          kind(static_cast<std::int16_t>(source.kind)), bounces(source.projectile_flags),
          bindings(&actual) {}
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(ProjectileFilter, kind) == 0x14);
static_assert(offsetof(ProjectileFilter, bounces) == 0x16);
static_assert(offsetof(ProjectileFilter, field_10) == 0x10);
#endif
struct DamageFilter {
    engine::TraceSkipFilterPrefix prefix;
    const GrenadeEngineBindings* bindings;
    GrenadeDamageFilterKind kind;
    static bool GRENADE_MEMBER should_hit(void* object, void* entity, std::uint32_t mask) {
        const auto& self = *static_cast<DamageFilter*>(object);
        const auto table = required_table(self.bindings->skip_filter_vtable);
        if (!reinterpret_cast<engine::MemberFunction<bool, void*, std::uint32_t>>(table[0])(
                object, entity, mask))
            return false;
        if (self.kind == GrenadeDamageFilterKind::ignore_entity)
            return true;
        // Full4333D32B conversion, followed by4334EA3E's two client exclusions.
        if (engine::invoke<bool>(self.bindings->static_props, 3, entity))
            return false;
        auto* converted = engine::invoke<void*>(entity, 7);
        if (!converted || engine::invoke<bool>(converted, 158))
            return false;
        return !engine::invoke<bool>(converted, 166);
    }
    static std::int32_t GRENADE_MEMBER trace_type(void* object) {
        const auto& self = *static_cast<DamageFilter*>(object);
        const auto table = required_table(self.bindings->skip_filter_vtable);
        return reinterpret_cast<engine::MemberFunction<std::int32_t>>(table[1])(object);
    }
    static void GRENADE_MEMBER set_entity(void* object, void* entity) {
        const auto& self = *static_cast<DamageFilter*>(object);
        const auto table = required_table(self.bindings->skip_filter_vtable);
        reinterpret_cast<engine::MemberFunction<void, void*>>(table[2])(object, entity);
    }
    static void GRENADE_MEMBER set_group(void* object, std::int32_t group) {
        const auto& self = *static_cast<DamageFilter*>(object);
        const auto table = required_table(self.bindings->skip_filter_vtable);
        reinterpret_cast<engine::MemberFunction<void, std::int32_t>>(table[3])(object, group);
    }
    static void* const* table() {
        static void* const methods[]{
            reinterpret_cast<void*>(should_hit), reinterpret_cast<void*>(trace_type),
            reinterpret_cast<void*>(set_entity), reinterpret_cast<void*>(set_group)};
        return methods;
    }
    DamageFilter(const GrenadeDamageFilter& source, const GrenadeEngineBindings& actual)
        : prefix{table(), pointer(source.ignored_entity), 0, 0}, bindings(&actual),
          kind(source.kind) {}
};
struct SmokeCollector {
    void* const* vtable;
    void** entities;
    std::int32_t capacity;
    const void* filter_class;
    std::int32_t count = 0;
    const EngineGrenades* engine;
    static std::int32_t GRENADE_MEMBER enumerate(void* object, void* handle_entity) {
        auto& self = *static_cast<SmokeCollector*>(object);
        // Native433E92B5 captures entity-list virtual4 before asking the
        // IHandleEntity for its handle, then reloads the list receiver.
        const auto& cells = self.engine->bindings();
        const auto table = *static_cast<void***>(cells.entity_list);
        const auto resolve =
            reinterpret_cast<engine::MemberFunction<void*, std::uint32_t>>(table[4]);
        const auto* handle = engine::invoke<const std::uint32_t*>(handle_entity, 2);
        const auto reference_handle = *handle;
        auto* entity = resolve(cells.entity_list, reference_handle);
        if (!entity)
            return 0;
        auto* base = engine::invoke<void*>(entity, 7);
        if (!base || (self.filter_class && self.engine->client_class(base) != self.filter_class))
            return 0;
        if (self.count >= self.capacity)
            return 1;
        self.entities[self.count++] = base;
        return 0;
    }
    static void* const* table() {
        static void* const methods[]{reinterpret_cast<void*>(enumerate)};
        return methods;
    }
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(SmokeCollector, count) == 0x10);
static_assert(offsetof(SmokeCollector, filter_class) == 0xC);
#endif
}

void* EngineGrenades::by_index(std::int32_t index) const {
    return engine::invoke<void*>(bindings_.entity_list, 3, index);
}
void* EngineGrenades::by_handle(std::uint32_t handle) const {
    return engine::invoke<void*>(bindings_.entity_list, 4, handle);
}
const void* EngineGrenades::client_class(void* entity) const {
    return engine::invoke<const void*>(static_cast<std::byte*>(entity) + 8, 2);
}
std::int32_t EngineGrenades::index(void* entity) const {
    return engine::invoke<std::int32_t>(static_cast<std::byte*>(entity) + 8, 10);
}
bool EngineGrenades::player(void* entity) const {
    return engine::invoke<bool>(entity, 158);
}
bool EngineGrenades::dormant(void* entity) const {
    return engine::invoke<bool>(static_cast<std::byte*>(entity) + 8, 9);
}
Vector3 EngineGrenades::live_origin(void* entity) const {
    return *engine::invoke<const Vector3*>(entity, 10);
}
GrenadeTraceProvider EngineGrenades::provider(std::uint8_t* flag) noexcept {
    return {this, trace_projectile, flag};
}
GrenadeTrace EngineGrenades::trace_projectile(void* context, const GrenadeRay& source,
                                              std::uint32_t mask,
                                              const GrenadeTraceFilter& filter) {
    auto& self = *static_cast<EngineGrenades*>(context);
    auto ray = native_ray(source);
    ProjectileFilter actual(filter, self.bindings_);
    MovementCollisionTrace result;
    initialize_trace(result);
    engine::invoke<void>(self.bindings_.engine_trace, 5, &ray, mask, &actual, &result);
    GrenadeTrace trace;
    std::memcpy(&trace.end, &result.end, sizeof trace.end);
    std::memcpy(&trace.normal, &result.plane_normal, sizeof trace.normal);
    std::memcpy(&trace.fraction, &result.fraction, sizeof trace.fraction);
    trace.contents = result.contents;
    trace.surface_flags = result.surface_flags;
    trace.all_solid = result.all_solid;
    trace.start_solid = result.start_solid;
    // The mandatory live collision overload performs native entity getters at
    // their actual post-trace sites. Trace packing must not pre-read them.
    trace.entity.identity = result.entity;
    return trace;
}
GrenadeDamageTrace EngineGrenades::trace(const GrenadeRay& source, std::uint32_t mask,
                                         const GrenadeDamageFilter& filter) {
    if (filter.kind != GrenadeDamageFilterKind::ignore_entity &&
        filter.kind != GrenadeDamageFilterKind::exclude_players)
        throw std::logic_error("Invalid grenade damage trace filter kind");
    auto ray = native_ray(source);
    DamageFilter actual(filter, bindings_);
    MovementCollisionTrace result;
    initialize_trace(result);
    engine::invoke<void>(bindings_.engine_trace, 5, &ray, mask, &actual, &result);
    GrenadeDamageTrace trace;
    std::memcpy(&trace.end, &result.end, sizeof trace.end);
    std::memcpy(&trace.fraction, &result.fraction, sizeof trace.fraction);
    trace.all_solid = result.all_solid != 0;
    trace.start_solid = result.start_solid != 0;
    trace.entity = result.entity;
    return trace;
}
std::uint32_t EngineGrenades::world_entity() {
    return identity(by_index(0));
}
std::uint32_t EngineGrenades::owner_handle(std::uint32_t entity) {
    return field<std::uint32_t>(pointer(entity), bindings_.offsets.owner_handle);
}
std::uint32_t EngineGrenades::entity_for_handle(std::uint32_t handle) {
    return identity(by_handle(handle));
}
std::uint32_t EngineGrenades::physics_object(std::uint32_t entity) {
    return identity(field<void*>(pointer(entity), bindings_.offsets.physics_object));
}
std::int32_t EngineGrenades::physics_material(std::uint32_t object) {
    return engine::invoke<std::int32_t>(pointer(object), 38);
}
float EngineGrenades::surface_density(std::int32_t material) {
    float density, thickness, friction, elasticity;
    engine::invoke<void>(bindings_.surface_properties, 4, material, &density, &thickness, &friction,
                         &elasticity);
    return density;
}
std::span<const Vector3> EngineGrenades::fire_cells(void* entity) {
    cells_.clear();
    const auto& o = bindings_.offsets;
    const auto origin = field<Vector3>(entity, o.origin);
    const auto count = field<std::int32_t>(entity, o.fire_count);
    for (std::int32_t ordinal = 0; ordinal < count; ++ordinal) {
        if (!field<std::uint8_t>(entity, o.fire_active + ordinal))
            continue;
        cells_.push_back({float(field<std::int32_t>(entity, o.fire_x + ordinal * 4)) + origin.x,
                          float(field<std::int32_t>(entity, o.fire_y + ordinal * 4)) + origin.y,
                          float(field<std::int32_t>(entity, o.fire_z + ordinal * 4)) + origin.z});
    }
    return cells_;
}
bool EngineGrenades::snapshot(void* entity, GrenadeWarningSnapshot& result) {
    if (!entity)
        return false;
    const auto& o = bindings_.offsets;
    result = {};
    result.identity = identity(entity);
    result.native_class = identity(client_class(entity));
    result.reference_handle = *engine::invoke<const std::uint32_t*>(entity, 2);
    result.index = index(entity);
    result.player = player(entity);
    result.dormant = dormant(entity);
    result.origin = field<Vector3>(entity, o.origin);
    result.virtual_origin = live_origin(entity);
    result.flags = field<std::uint32_t>(entity, o.effects);
    if (result.player) {
        result.team = field<std::int32_t>(entity, o.team);
        result.survival_team = field<std::int32_t>(entity, o.survival_team);
        if (result.dormant && bindings_.player_resource_slot) {
            auto* resource = field<void*>(bindings_.player_resource_slot, 0);
            if (resource && field<std::uint8_t>(resource, o.cached_team_valid + result.index)) {
                result.cached_team_valid = true;
                result.cached_team =
                    field<std::int32_t>(resource, o.cached_team + result.index * 4);
            }
        }
        result.velocity = field<Vector3>(entity, o.velocity);
        return true;
    }
    const auto native_class = pointer(result.native_class);
    if (native_class == bindings_.base_projectile_class ||
        native_class == bindings_.molotov_projectile_class) {
        result.velocity = field<Vector3>(entity, o.velocity);
        result.initial_velocity = field<Vector3>(entity, o.initial_velocity);
        result.simulation_time = field<float>(entity, o.simulation_time);
        result.explosion_effect_tick = field<std::int32_t>(entity, o.explosion_effect_tick);
        result.thrower_handle = field<std::uint32_t>(entity, o.thrower_handle);
        result.collision_group = field<std::int32_t>(entity, o.collision_group);
        result.projectile_flags = field<std::uint16_t>(entity, o.projectile_flags);
        if (native_class == bindings_.base_projectile_class)
            result.damage = field<float>(entity, o.damage);
    } else if (native_class == bindings_.inferno_class) {
        result.owner_handle = field<std::uint32_t>(entity, o.owner_handle);
        result.inferno_start_tick = field<std::int32_t>(entity, o.inferno_start_tick);
        result.fire_cells = fire_cells(entity);
    }
    return true;
}
bool EngineGrenades::snapshot_for_handle(std::uint32_t handle, GrenadeWarningSnapshot& result) {
    return snapshot(by_handle(handle), result);
}
bool EngineGrenades::snapshot_for_index(std::uint32_t index, GrenadeWarningSnapshot& result) {
    return snapshot(by_index(std::int32_t(index)), result);
}
std::span<void* const> EngineGrenades::collect_smokes(Vector3 center, float radius,
                                                      std::uint32_t mask, bool coarse) {
    const auto filter_class = bindings_.smoke_projectile_class;
    std::array<void*, 16> collected;
    SmokeCollector collector{SmokeCollector::table(), collected.data(), 16, filter_class, 0, this};
    engine::invoke<void>(bindings_.spatial_partition, 13, mask, &center, radius, coarse,
                         &collector);
    std::memcpy(smoke_entities_.data(), collected.data(),
                std::size_t(collector.count) * sizeof(void*));
    collected_smoke_class_ = filter_class;
    return {smoke_entities_.data(), std::size_t(collector.count)};
}
std::span<const GrenadeWarningPartitionEntity>
EngineGrenades::partition_entities(Vector3 center, float radius, std::uint32_t mask, bool coarse) {
    const auto entities = collect_smokes(center, radius, mask, coarse);
    for (std::size_t ordinal = 0; ordinal < entities.size(); ++ordinal) {
        auto* entity = entities[ordinal];
        auto& view = smoke_views_[ordinal];
        view.resolved = true;
        view.base_entity_present = true;
        view.native_class = identity(collected_smoke_class_);
        view.smoke_active = field<std::uint8_t>(entity, bindings_.offsets.smoke_active) != 0;
        if (view.smoke_active)
            view.origin = live_origin(entity);
    }
    return {smoke_views_.data(), entities.size()};
}
#undef GRENADE_MEMBER
} // namespace skeet::esp
