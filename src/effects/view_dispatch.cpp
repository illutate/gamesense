#include "skeet/effects/view_dispatch.hpp"

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_VIEW_PHASE __thiscall
#define SKEET_VIEW_FAST __fastcall
#else
#define SKEET_VIEW_PHASE
#define SKEET_VIEW_FAST
#endif
void SKEET_VIEW_PHASE empty_phase(void*) {} //433CCE8E
void SKEET_VIEW_FAST empty_player_view(void*, void*, std::uintptr_t, std::uintptr_t,
                                       std::uintptr_t) {} //4337D653
void SKEET_VIEW_FAST empty_view(void*, float*, float*, float*) {} //4334A269
void SKEET_VIEW_FAST empty_post(void*, void*) {} //433CCE8E
void SKEET_VIEW_FAST scope_view(void* receiver, void* player, std::uintptr_t, std::uintptr_t,
                                std::uintptr_t) {
    static_cast<ScopeViewObserver*>(receiver)->runtime->hide_scope_panel(player);
}
void SKEET_VIEW_PHASE sensitivity(void* receiver) {
    static_cast<FovViewObserver*>(receiver)->sensitivity->update();
}
void SKEET_VIEW_FAST fov_view(void* receiver, float*, float*, float* fov) {
    static_cast<FovViewObserver*>(receiver)->runtime->update(*fov);
}
const network::UpdateObserverVtable scope_vtable{
    {empty_phase, empty_phase, empty_phase, empty_phase, empty_phase},
    scope_view,
    empty_view,
    empty_post};
const network::UpdateObserverVtable fov_vtable{
    {sensitivity, empty_phase, empty_phase, empty_phase, empty_phase},
    empty_player_view,
    fov_view,
    empty_post};
#undef SKEET_VIEW_PHASE
#undef SKEET_VIEW_FAST
}
ScopeViewObserver::ScopeViewObserver(ViewRuntime& runtime) : endpoint{}, runtime(&runtime) {}
void ScopeViewObserver::initialize_vtable() noexcept {
    endpoint.vtable = &scope_vtable;
}
ScopeViewObserver::ScopeViewObserver(engine::OwnerRegistryBinding registry, ViewRuntime& runtime)
    : ScopeViewObserver(runtime) {
    network::register_update_observer(registry, endpoint);
    initialize_vtable();
}
FovViewObserver::FovViewObserver(misc::ZoomSensitivityRuntime& sensitivity, ViewFovRuntime& runtime)
    : endpoint{}, sensitivity(&sensitivity), runtime(&runtime) {}
void FovViewObserver::initialize_vtable() noexcept {
    endpoint.vtable = &fov_vtable;
}
FovViewObserver::FovViewObserver(engine::OwnerRegistryBinding registry,
                                 misc::ZoomSensitivityRuntime& sensitivity, ViewFovRuntime& runtime)
    : FovViewObserver(sensitivity, runtime) {
    network::register_update_observer(registry, endpoint);
    initialize_vtable();
}
void dispatch_player_view(const engine::OwnerRegistry& registry, void* player, std::uintptr_t a,
                          std::uintptr_t b, std::uintptr_t c) {
    const auto end = registry.end;
    for (auto current = registry.begin; current != end; ++current) {
        const auto owner = static_cast<network::UpdateObserver*>(*current);
        owner->vtable->player_view(owner, player, a, b, c);
    }
}
bool dispatch_view(const engine::OwnerRegistry& registry, float* origin, float* angles,
                   float* fov) {
    const auto end = registry.end;
    for (auto current = registry.begin; current != end; ++current) {
        const auto owner = static_cast<network::UpdateObserver*>(*current);
        owner->vtable->view(owner, origin, angles, fov);
    }
    return false;
}
} // namespace skeet::effects
