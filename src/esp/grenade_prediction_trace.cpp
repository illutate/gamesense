#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/esp/grenade_environment.hpp"
#include "skeet/engine/interface_call.hpp"

namespace skeet::esp::grenade_detail {
namespace {
struct SnapshotTraceEntities {};
bool is_player(const GrenadeHitEntity& entity, const SnapshotTraceEntities&) noexcept {
    return entity.player;
}
bool is_player(const GrenadeHitEntity& entity, const GrenadeEnvironmentBindings&) {
    return engine::invoke<bool>(reinterpret_cast<void*>(std::uintptr_t(entity.identity)), 158);
}
GrenadeRay ray(Vector3 start, Vector3 displacement, bool hull) {
    const Vector3 delta{(start.x + displacement.x) - start.x, (start.y + displacement.y) - start.y,
                        (start.z + displacement.z) - start.z};
    return {start, delta, hull ? Vector3{2, 2, 2} : Vector3{},
            (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f, !hull};
}
}
template <class Environment>
GrenadeTrace move_impl(GrenadeState& state, Vector3 displacement, GrenadeTraceProvider provider,
                       const Environment& environment) {
    const auto start = state.position;
    auto mask = state.collision_group == 1 ? 0x8400bu : 0x4208608bu;
    auto input = ray(start, displacement, true);
    auto filter =
        GrenadeTraceFilter{state.owner_entity, state.ignored_handle, state.collision_group,
                           std::uint16_t(state.bounces), state.kind};
    auto hit = provider.trace(provider.context, input, mask, filter);
    if (hit.start_solid && (hit.contents & 0x80000u))
        hit = provider.trace(provider.context, input, mask & 0xfff7ffffu, filter);
    if (hit.fraction < 1.0f || hit.all_solid || hit.start_solid) {
        if (hit.entity.identity && is_player(hit.entity, environment)) {
            input = ray(start, displacement, false);
            hit = provider.trace(provider.context, input, mask, filter);
        }
        if (hit.start_solid) {
            state.collision_group = 3;
            filter.collision_group = 3;
            const Vector3 from{start.x - displacement.x, start.y - displacement.y,
                               start.z - displacement.z};
            const Vector3 delta{(displacement.x + start.x) - from.x,
                                (displacement.y + start.y) - from.y,
                                (displacement.z + start.z) - from.z};
            input = {from,
                     delta,
                     {},
                     (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0,
                     true};
            hit = provider.trace(provider.context, input, 0x8400bu, filter);
        }
    }
    if (hit.fraction != 0.0f)
        state.position = hit.end;
    return hit;
}
GrenadeTrace move(GrenadeState& state, Vector3 displacement, GrenadeTraceProvider provider) {
    return move_impl(state, displacement, provider, SnapshotTraceEntities{});
}
GrenadeTrace move(GrenadeState& state, Vector3 displacement, GrenadeTraceProvider provider,
                  const GrenadeEnvironmentBindings& environment) {
    return move_impl(state, displacement, provider, environment);
}
} // namespace skeet::esp::grenade_detail
