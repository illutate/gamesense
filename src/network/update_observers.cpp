#include "skeet/network/update_observers.hpp"

namespace skeet::network {
namespace {
void dispatch(std::span<void* const> entries, std::size_t phase) {
    for (auto* object : entries) {
        auto* observer = static_cast<UpdateObserver*>(object);
        observer->vtable->phases[phase](object);
    }
}
}
void* register_update_observer(engine::OwnerRegistryBinding registry, UpdateObserver& observer) {
    return registry.append(&observer);
}
void dispatch_update_phase0(const engine::OwnerRegistry& registry, std::int32_t& stage) {
    const auto entries = registry.entries();
    stage = 5;
    dispatch(entries, 0);
}
void dispatch_update_phase1(const engine::OwnerRegistry& registry, std::int32_t& stage) {
    const auto entries = registry.entries();
    stage = 6;
    dispatch(entries, 1);
}
void dispatch_update_phase2(const engine::OwnerRegistry& registry) {
    dispatch(registry.entries(), 2);
}
void dispatch_update_phase3(const engine::OwnerRegistry& registry) {
    dispatch(registry.entries(), 3);
}
void dispatch_update_phase4(const engine::OwnerRegistry& registry) {
    dispatch(registry.entries(), 4);
}
} // namespace skeet::network
