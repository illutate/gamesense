#include "skeet/hud/penetration_runtime.hpp"
#include "skeet/rage/targeting_commit.hpp"
#include "skeet/rage_segments.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skeet::hud {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
void tick_field(void* object, std::ptrdiff_t offset, std::int32_t value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t absolute_difference(std::int32_t first, std::int32_t second) noexcept {
    const auto difference = std::uint32_t(first) - std::uint32_t(second);
    const auto sign = std::bit_cast<std::int32_t>(difference) < 0 ? UINT32_MAX : 0u;
    return std::bit_cast<std::int32_t>((difference ^ sign) - sign);
}
// The first0x24 bytes are the native4346F784 filter prefix. A real engine
// TraceRay receives this object and invokes the source predicate through its
// actual member-function ABI; the remaining four slots retain engine methods.
struct ReticleTraceFilter {
    void** vtable;
    std::array<std::uint32_t, 5> fields_04{};
    std::uint32_t mask = 0x4600400b, exit_mask = 0x4600400b;
    float minimum_damage = 1.0f;
    const PenetrationReticleEnvironment* context;
    std::array<void*, 5> methods;
    ReticleTraceFilter(const PenetrationReticleEnvironment& bindings, BallisticEntity preferred)
        : vtable(nullptr), context(&bindings) {
        if (!context->trace_filter_vtable)
            throw std::logic_error("Required engine trace filter table is not bound");
        fields_04[4] = preferred;
        const auto base = context->trace_filter_vtable;
        methods = {reinterpret_cast<void*>(should_hit), base[1], base[2], base[3], base[4]};
        vtable = methods.data();
    }
#if defined(_M_IX86) || defined(__i386__)
    static bool __thiscall should_hit(void* receiver, BallisticEntity entity, std::uint32_t mask){
#else
    static bool should_hit(void* receiver, BallisticEntity entity, std::uint32_t mask) {
#endif
        auto& self = *static_cast<ReticleTraceFilter*>(receiver);
    if (entity && entity != self.fields_04[4]) {
        auto* current = engine::invoke<void*>(reinterpret_cast<void*>(std::uintptr_t(entity)), 7);
        while (current) {
            if (field<std::int32_t>(current, self.context->collision_group) == 11 ||
                engine::invoke<bool>(current, 158))
                return false;
            const auto handle = field<std::int32_t>(current, self.context->move_parent_handle);
            if (handle == -1)
                break;
            current = engine::invoke<void*>(self.context->targeting.entity_list, 4, handle);
        }
    }
    const auto delegate =
        reinterpret_cast<engine::MemberFunction<bool, BallisticEntity, std::uint32_t>>(
            self.context->trace_filter_vtable[0]);
    return delegate(receiver, entity, mask);
} BallisticFilter binding() noexcept {
    return {this, fields_04[4], mask, exit_mask, minimum_damage};
}
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(offsetof(ReticleTraceFilter, mask) == 0x18);
static_assert(offsetof(ReticleTraceFilter, minimum_damage) == 0x20);
#endif
// All bullet/exit/penetration decisions stay in the recovered implementations.
// Software entity clipping and engine TraceRay both invoke the same ABI filter.
class ReticleBallistics final : public BallisticEngine {
    BallisticEngine& inner;

public:
    explicit ReticleBallistics(BallisticEngine& engine) : inner(engine) {}
    std::int32_t filter_type(const BallisticFilter& f) override {
        return engine::invoke<std::int32_t>(f.object, 1);
    }
    void bind_source(const BallisticFilter& f, BallisticEntity e) override {
        engine::invoke<void>(f.object, 2, e);
    }
    void bind_last_hit(const BallisticFilter& f, BallisticEntity e) override {
        engine::invoke<void>(f.object, 4, e);
    }
    bool accepts(const BallisticFilter& f, BallisticEntity entity, std::uint32_t mask) override {
        return engine::invoke<bool>(f.object, 0, entity, mask);
    }
    std::uint32_t point_contents(const BallisticVector& p, std::uint32_t m, bool w) override {
        return inner.point_contents(p, m, w);
    }
    void trace_ray(const BallisticTraceRequest& r, BallisticTrace& t) override {
        inner.trace_ray(r, t);
    }
    void clip_ray_to_entity(const BallisticRay& r, std::uint32_t m, BallisticEntity e,
                            BallisticTrace& t) override {
        inner.clip_ray_to_entity(r, m, e, t);
    }
    PenetrationSurface surface(std::int16_t p) override {
        return inner.surface(p);
    }
    BallisticEntity entity(std::int32_t i) override {
        return inner.entity(i);
    }
    std::int32_t maximum_clients() override {
        return inner.maximum_clients();
    }
    BallisticEntityInfo entity_info(BallisticEntity e) override {
        return inner.entity_info(e);
    }
    BallisticEntityInfo clip_entity_info(BallisticEntity e) override {
        return inner.clip_entity_info(e);
    }
    BallisticBreakableInfo breakable_info(BallisticEntity e) override {
        return inner.breakable_info(e);
    }
    BallisticModelInfo model_info(std::int32_t i) override {
        return inner.model_info(i);
    }
    std::uint8_t damage_mode(BallisticEntity e) override {
        return inner.damage_mode(e);
    }
    void set_damage_mode(BallisticEntity e, std::uint8_t m) override {
        inner.set_damage_mode(e, m);
    }
    bool has_engine_breakable_callback() override {
        return inner.has_engine_breakable_callback();
    }
    bool engine_breakable(BallisticEntity e) override {
        return inner.engine_breakable(e);
    }
    BallisticRules rules() override {
        return inner.rules();
    }
};
}
void* closest_penetration_target(const rage::TargetBindings& bindings, std::int32_t maximum_clients,
                                 const RageVector& origin, const RageVector& direction,
                                 float maximum_distance) { // 0x4332AA97
    void* selected = nullptr;
    for (std::int32_t index = 1; index <= maximum_clients; ++index) {
        auto* target = engine::invoke<void*>(bindings.entity_list, 3, index);
        if (!rage::target_is_eligible(bindings, target))
            continue;
        const auto bottom = field<RageVector>(target, bindings.offsets.origin);
        const auto x = bottom[0] - origin[0], y = bottom[1] - origin[1], z = bottom[2] - origin[2];
        const auto magnitude = std::sqrt((x * x + z * z) + y * y);
        const auto cosine =
            ((direction[1] * y + direction[0] * x) + direction[2] * z) / (magnitude + 0x1p-23f);
        if (0.0f > cosine)
            continue;
        auto top = bottom;
        top[2] += (*engine::invoke<const RageVector*>(target, 77))[2];
        const auto closest = closest_rage_ray_segment({origin, direction}, {bottom, top});
        const auto other = 1.0f - closest.second_fraction;
        const auto dx =
            closest.first_point[0] - (bottom[0] * other + top[0] * closest.second_fraction);
        const auto dy =
            closest.first_point[1] - (bottom[1] * other + top[1] * closest.second_fraction);
        const auto dz =
            closest.first_point[2] - (bottom[2] * other + top[2] * closest.second_fraction);
        const auto distance = std::sqrt((dx * dx + dy * dy) + dz * dz);
        if (!(distance >= maximum_distance)) {
            maximum_distance = distance;
            selected = target;
        }
    }
    return selected;
}
void update_penetration_reticle(PenetrationReticleState& state,
                                const esp::ScreenOverlayConfig& config,
                                const PenetrationReticleFrame& frame,
                                const PenetrationReticleEnvironment& context) { // 0x4334B1DA
    state.color = 0xff3200ff;
    state.ready = false;
    if (!config.penetration_reticle.value || !frame.weapon)
        return;
    const auto identifier =
        field<std::uint16_t>(frame.weapon, context.targeting.offsets.weapon_identifier);
    if (!lagcomp::history_weapon_is_firearm(identifier))
        return;
    auto* closest = closest_penetration_target(context.targeting, *context.maximum_clients,
                                               frame.origin, frame.direction, 60.0f);
    void* changed = nullptr;
    std::int32_t saved_tick = 0;
    if (closest && context.predicted_tick) {
        saved_tick = field<std::int32_t>(closest, context.target_tick);
        if (absolute_difference(saved_tick, *context.predicted_tick) < 64) {
            tick_field(closest, context.target_tick, *context.predicted_tick);
            changed = closest;
        }
    }
    auto query = rage::prepare_target_bullet(frame.weapon,
                                             context.targeting.offsets.weapon_identifier, true);
    ReticleTraceFilter native_filter(context,
                                     std::uint32_t(reinterpret_cast<std::uintptr_t>(closest)));
    auto filter = native_filter.binding();
    ReticleBallistics filtered_engine(context.ballistics);
    const auto source = std::uint32_t(reinterpret_cast<std::uintptr_t>(frame.local_player));
    const auto result = rage::trace_target_direction(filtered_engine, source, frame.origin,
                                                     frame.direction, filter, query);
    auto targeting = context.targeting;
    targeting.local_player = frame.local_player;
    auto* hit = reinterpret_cast<void*>(std::uintptr_t(query.trace.entity));
    if (result != 0 && hit && engine::invoke<bool>(hit, 158) &&
        rage::target_is_enemy(targeting, hit)) {
        state.color = 0xffff6012;
        state.ready = true;
    } else {
        state.ready = true;
        if (std::bit_cast<std::int32_t>(query.point_count) > 0) {
            const auto& first = query.points[0];
            const auto& last = query.trace.end;
            const auto x = first[0] - last[0], y = first[1] - last[1], z = first[2] - last[2];
            if ((y * y + x * x) + z * z >= 1000.0f) {
                const RageVector midpoint{(last[0] - first[0]) * 0.5f + first[0],
                                          (last[1] - first[1]) * 0.5f + first[1],
                                          (last[2] - first[2]) * 0.5f + first[2]};
                if (engine::invoke<std::int32_t>(context.point_contents, 0, &midpoint, 0x10001u,
                                                 static_cast<void**>(nullptr)) == 0)
                    state.color = 0xff32ff00;
            }
        }
    }
    if (changed)
        tick_field(changed, context.target_tick, saved_tick);
}
}
