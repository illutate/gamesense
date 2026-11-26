#include "skeet/anti_aim/proximity.hpp"

namespace skeet::anti_aim {
namespace {
bool nearby(ProximityEngine& engine, ProximityEntity candidate, ProximityEntity local) {
    const auto first = engine.origin(candidate);
    const auto second = engine.origin(local);
    const auto y = first[1] - second[1], x = first[0] - second[0], z = first[2] - second[2];
    return (y * y + x * x) + z * z < 10000.0f;
}
}
bool any_nearby_entity(ProximityEngine& engine, ProximityEntity local) {
    const auto* list = engine.list();
    if (!list || list->count <= 0)
        return false;
    for (std::int32_t index = 0; index < list->count; ++index) {
        const auto candidate = list->entities[index];
        if (candidate && engine.active(candidate)) {
            if (nearby(engine, candidate, local))
                return true;
            list = engine.list();
        }
    }
    return false;
}
bool first_nearby_entity(ProximityEngine& engine, ProximityEntity local) {
    const auto* list = engine.list();
    if (!list || list->count == 0)
        return false;
    const auto first = list->entities[0];
    return first && engine.active(first) && nearby(engine, first, local);
}
} // namespace skeet::anti_aim
