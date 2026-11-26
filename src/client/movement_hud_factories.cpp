#include "skeet/client/movement_hud_factories.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/engine/native_data_image.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
using Bindings = MovementHudFactoryBindings;

void initialize_movement(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    auto* const storage =
        engine::allocate_owner_storage(b.movement_registries.lifecycle.memory,
                                       (std::max)(std::size_t(0x578), movement_owner_size()));
    if (!storage) {
        b.movement.active = nullptr; //434856ED/EF
        return;
    }
    // The native process-heap allocation zeroed the embedded state before
    // construction. Project that backing into the same borrowed source state.
    b.movement.command.state = MovementCommandOwner{};
    construct_movement_owner(storage, b.movement_registries, b.configuration, b.movement);
}

void initialize_blockbot(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    const auto& registries = b.movement_registries;
    auto* const storage = engine::allocate_owner_storage(
        registries.lifecycle.memory, (std::max)(std::size_t(0x68), blockbot_owner_size()));
    //4348DA67's protected INT1 continuation is not recovered. Surface the
    //allocation failure instead of inventing its native control flow.
    if (!storage)
        throw std::bad_alloc{};
    b.blockbot_settings = BlockbotConfig{};
    b.blockbot_state = BlockbotState{};
    (void)construct_blockbot_owner(storage, registries.lifecycle, registries.commands,
                                   registries.hud, b.configuration, b.blockbot_settings,
                                   b.blockbot_state, b.blockbot, b.blockbot_projection,
                                   b.blockbot_life_state_offset);
}

void initialize_overlay(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    auto* const storage =
        hud::allocate_screen_overlay_owner_storage(b.overlay_registries.lifecycle.memory);
    //43490971 is an unrecovered INT1 allocation-failure continuation.
    if (!storage)
        throw std::bad_alloc{};
    hud::construct_allocated_screen_overlay_owner(storage, b.overlay_registries, b.configuration,
                                                  b.screen_settings, b.overlay_command,
                                                  b.overlay_rendering);
}

void initialize_reticle(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    auto* const storage =
        hud::allocate_penetration_reticle_owner_storage(b.reticle_registries.lifecycle.memory);
    //43481DF7 is an unrecovered INT1 allocation-failure continuation.
    if (!storage)
        throw std::bad_alloc{};
    hud::construct_allocated_penetration_reticle_owner(
        storage, b.reticle_registries, b.configuration, b.screen_settings, b.reticle_command,
        b.reticle_environment, b.engine_trace, b.reticle_rendering);
}

bool same_memory(engine::JobMemory first, engine::JobMemory second) noexcept {
    return first.allocate == second.allocate && first.free == second.free &&
           first.process_heap == second.process_heap;
}

void require_bindings(NativeStartupSources& sources, const Bindings& b) {
    const auto& image = sources.data();
    const auto memory = b.movement_registries.lifecycle.memory;
    if (!memory.allocate || !memory.free || !memory.process_heap)
        throw std::invalid_argument("Movement/HUD factories require the canonical process heap");
    struct RegistryAlias {
        const engine::OwnerRegistryBinding* binding;
        std::uint32_t address;
    };
    const std::array aliases{RegistryAlias{&b.movement_registries.lifecycle, 0x43475B78u},
                             RegistryAlias{&b.movement_registries.hud, 0x43475A9Cu},
                             RegistryAlias{&b.overlay_registries.lifecycle, 0x43475B78u},
                             RegistryAlias{&b.overlay_registries.hud, 0x43475A9Cu},
                             RegistryAlias{&b.reticle_registries.lifecycle, 0x43475B78u},
                             RegistryAlias{&b.reticle_registries.hud, 0x43475A9Cu}};
    for (const auto& alias : aliases)
        if (&alias.binding->registry != &image.native_cell<engine::OwnerRegistry>(alias.address) ||
            !same_memory(alias.binding->memory, memory))
            throw std::invalid_argument(
                "Movement/HUD factories must share their native registry cells and heap");
    const auto& commands = b.movement_registries.commands;
    if (&b.overlay_registries.commands != &commands ||
        &b.reticle_registries.commands != &commands || &commands.image() != &image ||
        &commands.native_storage() != &image.native_cell<engine::OwnerRegistry>(0x434757A0u))
        throw std::invalid_argument(
            "Movement/HUD factories must share their canonical command registry");
    if (&b.configuration.descriptors != &image.native_cell<ConfigLink>(0x434798BCu) ||
        &b.configuration.hotkeys != &image.native_cell<HotkeyLink>(0x434798C4u) ||
        &b.movement.active != &image.native_cell<MovementCommandOwner*>(0x434759B0u))
        throw std::invalid_argument(
            "Movement/HUD factories require canonical configuration and movement publication cells");
    if (&b.overlay_command.settings != &b.screen_settings ||
        &b.overlay_rendering.config != &b.screen_settings ||
        &b.overlay_rendering.state != &b.overlay_command.state ||
        &b.reticle_command.settings != &b.screen_settings ||
        &b.reticle_rendering.config != &b.screen_settings ||
        &b.reticle_rendering.state != &b.reticle_command.state ||
        &b.reticle_command.bindings != &b.reticle_environment)
        throw std::invalid_argument(
            "Movement/HUD factories must share canonical command and render state");
    if (&b.engine_trace != &image.native_cell<void*>(0x43468304u) ||
        &b.overlay_command.bindings.history_owner !=
            &image.native_cell<EntityCommandOwner*>(0x43475898u))
        throw std::invalid_argument(
            "Movement/HUD factories require live engine-trace and command-history cells");
}
}

void bind_movement_hud_factories(NativeStartupSources& sources,
                                 MovementHudFactoryBindings& bindings) {
    require_bindings(sources, bindings);
    const std::array calls{NativeInitializerCall{0x434856D6u, &bindings, initialize_movement},
                           NativeInitializerCall{0x4348DA54u, &bindings, initialize_blockbot},
                           NativeInitializerCall{0x4349095Eu, &bindings, initialize_overlay},
                           NativeInitializerCall{0x43481DE4u, &bindings, initialize_reticle}};
    for (const auto& call : calls)
        sources.bind_initializer(call);
}
} //namespace skeet::client
