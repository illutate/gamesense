#include "skeet/client/world_runtime_factories.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/command_phase_registry.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
using Bindings = WorldRuntimeFactoryBindings;
Bindings& bindings(void* opaque) {
    return *static_cast<Bindings*>(opaque);
}
void* allocate(Bindings& source, std::size_t native_size, std::size_t source_size) {
    return engine::allocate_owner_storage(source.lifecycle.memory,
                                          std::max(native_size, source_size));
}
void* required_storage(Bindings& source, std::size_t native_size, std::size_t source_size) {
    auto* storage = allocate(source, native_size, source_size);
    // The five original OOM targets begin INT1 and remain unresolved. Throwing
    // bad_alloc is an explicit source failure policy, not a recovered VM edge.
    if (!storage)
        throw std::bad_alloc();
    return storage;
}
void smoke(void* opaque) { //4347D8D0 ->43480340
    auto& source = bindings(opaque);
    auto* storage = allocate(source, 0x20, effects::smoke_owner_size());
    if (!storage) {
        source.smoke.active = nullptr;
        return;
    }
    effects::construct_smoke_owner(storage, {source.lifecycle, source.updates},
                                   source.configuration, source.smoke);
}
void scope(void* opaque) { //4348E3F9 ->43486002
    auto& source = bindings(opaque);
    auto* storage = required_storage(source, 0x2C, effects::scope_owner_size());
    effects::construct_scope_owner(storage, {source.lifecycle, source.hud, source.updates},
                                   source.configuration, source.scope);
}
void game_system(void* opaque) { //4348E09F, including its nullable allocation path
    auto& source = bindings(opaque);
    auto& context = source.game_system;
    engine::create_game_system_owner(source.lifecycle, context.manager, context.allocator,
                                     context.runtime);
}
void auto_accept(void* opaque) { //43481D50 ->4348E0E8
    auto& source = bindings(opaque);
    auto* storage = allocate(source, 0x28, misc::auto_accept_owner_size());
    if (!storage) {
        source.auto_accept.active_owner = nullptr;
        return;
    }
    misc::construct_auto_accept(storage, source.lifecycle, source.configuration,
                                source.auto_accept);
}
void reveal_ranks(void* opaque) { //4348038F ->4347E8A6
    auto& source = bindings(opaque);
    auto& context = source.reveal_ranks;
    // The existing whole factory allocates its larger source instance and
    // publishes null to both canonical consumers on allocation failure.
    misc::initialize_reveal_ranks(source.lifecycle, source.configuration, context.settings,
                                  context.active, context.engine);
}
void reveal_overwatch(void* opaque) { //43490411 ->4348E4CC
    auto& source = bindings(opaque);
    auto& context = source.reveal_overwatch;
    auto* storage = required_storage(source, 0x24, misc::reveal_overwatch_owner_size());
    misc::construct_reveal_overwatch(storage, source.lifecycle, source.updates,
                                     source.configuration, context.settings, context.engine);
}
void disable_sv_pure(void* opaque) { //43484ED9 ->434864C4
    auto& source = bindings(opaque);
    auto& context = source.disable_sv_pure;
    auto* storage = allocate(source, 0x20, misc::disable_sv_pure_owner_size());
    if (!storage) {
        context.active = nullptr;
        return;
    }
    auto& owner = misc::construct_disable_sv_pure_owner(storage, source.lifecycle, source.messages,
                                                        source.configuration, context.settings,
                                                        context.active);
    context.active = &owner;
}
void game_system_registration(void* opaque) { //4347DA14
    auto& source = bindings(opaque);
    auto& context = source.game_system_registration;
    engine::create_game_system_registration(source.lifecycle, context.head_slot, context.callbacks,
                                            context.unload);
}
void persistent_kill_feed(void* opaque) { //43481E08 ->4347E910
    auto& source = bindings(opaque);
    auto& context = source.persistent_kill_feed;
    auto* storage = required_storage(source, 0x3C, misc::persistent_kill_feed_owner_size());
    misc::construct_persistent_kill_feed_owner(storage, source.lifecycle, source.event_observers,
                                               source.frames, source.configuration,
                                               context.settings, context.engine);
}
void hit_markers(void* opaque) { //43484954 ->43486513
    auto& source = bindings(opaque);
    auto& context = source.hit_markers;
    auto* storage = required_storage(source, 0x4C, events::hit_marker_owner_size());
    events::construct_hit_marker(storage, source.lifecycle, source.hud, source.configuration,
                                 context.events, context.bindings);
}
void window_lifecycle(void* opaque) { //4347F902 ->4347F555
    auto& source = bindings(opaque);
    auto& context = source.window_lifecycle;
    auto* storage = required_storage(source, 0xC0, input::native_window_lifecycle_owner_size());
    input::construct_native_window_lifecycle(storage, source.lifecycle, source.command_owners,
                                             source.updates, context.persistence, context.imports,
                                             context.modules, context.images);
}
void harmless_molotov(void* opaque) { //43490982 ->43484AEA
    auto& source = bindings(opaque);
    auto* storage = allocate(source, 0xA4, effects::harmless_molotov_owner_size());
    if (!storage) {
        source.harmless_molotov.effects.active = nullptr;
        return;
    }
    effects::construct_harmless_molotov_owner(storage, {source.lifecycle, source.updates},
                                              source.configuration, source.harmless_molotov);
}
} //namespace

void bind_world_runtime_factories(NativeStartupSources& sources,
                                  WorldRuntimeFactoryBindings& source) {
    if (&sources.data() != &source.command_owners.image())
        throw std::invalid_argument(
            "World factories require the scheduler's canonical command registry");
    if (&source.scope.runtime.settings != &source.scope.settings ||
        &source.smoke.runtime.settings != &source.scope.settings ||
        &source.harmless_molotov.settings != &source.scope.settings)
        throw std::invalid_argument("World factories require one canonical effects configuration");
    auto& system = source.game_system.runtime;
    if (&system.bindings.disable_pure != &source.disable_sv_pure.settings)
        throw std::invalid_argument("GameSystem and DisableSvPure require the same configuration");
    if (&system.updates != &source.updates.registry || &system.frames != &source.frames.registry ||
        &system.shutdown.owners != &source.lifecycle.registry ||
        &source.game_system_registration.unload.owners != &source.lifecycle.registry)
        throw std::invalid_argument(
            "World factories require the canonical dispatch and shutdown registries");
    sources.bind_initializer({0x4347D8D0u, &source, smoke});
    sources.bind_initializer({0x4348E3F9u, &source, scope});
    sources.bind_initializer({0x4348E09Fu, &source, game_system});
    sources.bind_initializer({0x43481D50u, &source, auto_accept});
    sources.bind_initializer({0x4348038Fu, &source, reveal_ranks});
    sources.bind_initializer({0x43490411u, &source, reveal_overwatch});
    sources.bind_initializer({0x43484ED9u, &source, disable_sv_pure});
    sources.bind_initializer({0x4347DA14u, &source, game_system_registration});
    sources.bind_initializer({0x43481E08u, &source, persistent_kill_feed});
    sources.bind_initializer({0x43484954u, &source, hit_markers});
    sources.bind_initializer({0x4347F902u, &source, window_lifecycle});
    sources.bind_initializer({0x43490982u, &source, harmless_molotov});
}
} //namespace skeet::client
