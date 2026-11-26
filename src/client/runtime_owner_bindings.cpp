#include "skeet/client/runtime_owner_bindings.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/script/client_api.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
using Bindings = RuntimeOwnerFactoryBindings;

void initialize_sequences(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    (void)skins::initialize_sequence_owner(b.lifecycle, b.entity_updates, b.configuration,
                                           b.sequences);
}
void initialize_rage(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    (void)rage::construct_rage_owner(b.rage, b.rage_active);
}
void initialize_legit(void* owner) {
    (void)legit::create_legit_owner(static_cast<Bindings*>(owner)->legit);
}
void initialize_esp(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    esp::initialize_esp_owner(b.esp_active, b.esp);
}
void initialize_blur(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    effects::initialize_blur(b.lifecycle, b.effect_frames, b.blur_active, b.blur);
}
void initialize_console(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    (void)misc::initialize_console_output_owner(b.lifecycle, b.hud, b.configuration,
                                                b.console_settings, b.console_active, b.frame_time);
}
void initialize_fov(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    auto* storage = engine::allocate_owner_storage(
        b.lifecycle.memory, std::max(std::size_t{0x34}, misc::fov_owner_size()));
    //4347DA73 is an unresolved INT1 allocation-failure edge. This source
    //factory fails explicitly rather than inventing a null continuation.
    if (!storage)
        throw std::bad_alloc();
    std::memset(&b.fov_settings, 0, sizeof b.fov_settings);
    misc::construct_fov_owner(storage, b.lifecycle, b.esp.registries.updates, b.configuration,
                              b.fov_settings, b.fov);
}
void initialize_purchase_log(void* owner) {
    auto& b = *static_cast<Bindings*>(owner);
    auto* storage = engine::allocate_owner_storage(
        b.lifecycle.memory, std::max(std::size_t{0xE4}, misc::purchase_log_owner_size()));
    //43481F59 reaches unresolved INT1 on OOM; bad_alloc is source policy.
    if (!storage)
        throw std::bad_alloc();
    misc::construct_purchase_log(storage, b.lifecycle, b.esp.registries.frame_update,
                                 b.configuration, b.sequences.events, b.event_output,
                                 b.event_targets, b.fov.entity_list, b.purchase_console,
                                 b.console_container);
}

bool same_memory(engine::JobMemory first, engine::JobMemory second) noexcept {
    return first.allocate == second.allocate && first.free == second.free &&
           first.process_heap == second.process_heap;
}
void require_bindings(NativeStartupSources& sources, const Bindings& b) {
    const auto& image = sources.data();
    if (!b.lifecycle.memory.allocate || !b.lifecycle.memory.free ||
        !b.lifecycle.memory.process_heap)
        throw std::invalid_argument("Runtime owner factories require their canonical process heap");
    struct RegistryAlias {
        const engine::OwnerRegistryBinding* binding;
        std::uint32_t address;
    };
    const std::array aliases{RegistryAlias{&b.lifecycle, 0x43475b78u},
                             RegistryAlias{&b.hud, 0x43475a9cu},
                             RegistryAlias{&b.effect_frames, 0x43475a84u},
                             RegistryAlias{&b.rage.lifecycle, 0x43475b78u},
                             RegistryAlias{&b.rage.hud_owners, 0x43475a9cu},
                             RegistryAlias{&b.legit.lifecycle, 0x43475b78u},
                             RegistryAlias{&b.esp.registries.owners, 0x43475b78u},
                             RegistryAlias{&b.esp.registries.hud, 0x43475a9cu},
                             RegistryAlias{&b.esp.registries.updates, 0x43475988u},
                             RegistryAlias{&b.esp.registries.packets, 0x4347584cu},
                             RegistryAlias{&b.esp.registries.frame_update, 0x43475ad8u},
                             RegistryAlias{&b.esp.registries.map_parser, 0x43475770u}};
    for (const auto& alias : aliases)
        if (&alias.binding->registry != &image.native_cell<engine::OwnerRegistry>(alias.address) ||
            !same_memory(alias.binding->memory, b.lifecycle.memory))
            throw std::invalid_argument(
                "Runtime owner factories must share their native registry cells and heap");
    if (&b.configuration.descriptors != &image.native_cell<ConfigLink>(0x434798bcu) ||
        &b.configuration.hotkeys != &image.native_cell<HotkeyLink>(0x434798c4u) ||
        &b.rage.configuration != &b.configuration || &b.legit.configuration != &b.configuration ||
        &b.esp.registries.configuration != &b.configuration)
        throw std::invalid_argument(
            "Runtime owner factories must share the canonical configuration registry");
    if (&b.rage.command_owners != &b.legit.command_owners ||
        &b.rage.command_owners.image() != &image || &b.legit.empty_tick_owners.image() != &image ||
        &b.rage.entity_owners != &b.entity_updates ||
        &b.esp.registries.histories != &b.entity_updates || &b.entity_updates.image() != &image ||
        &b.entity_updates.native_storage() !=
            &image.native_cell<engine::OwnerRegistry>(0x43475ba8u))
        throw std::invalid_argument(
            "Runtime owner factories must share their command and entity observer registries");
    if (&b.rage_active != &image.native_cell<rage::LifecycleOwner*>(0x43475810u) ||
        &b.legit.active != &image.native_cell<legit::LifecycleOwner*>(0x4347588cu) ||
        &b.esp_active != &image.native_cell<esp::EspOwnerRuntime*>(0x434758dcu) ||
        &b.blur_active != &image.native_cell<effects::BlurEffect*>(0x43475a20u) ||
        &b.console_active != &image.native_cell<misc::ColoredLogRing*>(0x434758ccu))
        throw std::invalid_argument(
            "Runtime owner factories require their canonical published owner slots");
    if (&b.sequences.settings != &b.sequences.receive.bindings.settings ||
        &b.sequences.state != &b.sequences.receive.state ||
        &b.sequences.events != &b.esp.registries.events)
        throw std::invalid_argument(
            "Runtime owner factories must share their sequence state and event registry");
    if (&b.fov.entity_list != &image.native_cell<void*>(0x4346a890u) ||
        &b.fov.sensitivity.local_player != &image.native_cell<void*>(0x43475808u) ||
        &b.fov.sensitivity.override_context != &image.native_cell<void*>(0x43468994u) ||
        &b.fov.sensitivity.encoded_mouse_sensitivity_cvar !=
            &image.native_cell<std::uint32_t>(0x43467a48u) ||
        &b.console_container != &image.native_cell<void*>(0x43467d84u) ||
        b.event_output.hud_slot != &b.console_active ||
        b.event_output.script != &b.purchase_console.bridge ||
        &b.purchase_console.storage.image() != &image)
        throw std::invalid_argument(
            "FOV and event owners require canonical entity and hook storage");
}
}

void bind_runtime_owner_factories(NativeStartupSources& sources,
                                  RuntimeOwnerFactoryBindings& bindings) {
    require_bindings(sources, bindings);
    const std::array calls{NativeInitializerCall{0x43481f6au, &bindings, initialize_sequences},
                           NativeInitializerCall{0x4348da25u, &bindings, initialize_rage},
                           NativeInitializerCall{0x4347febcu, &bindings, initialize_legit},
                           NativeInitializerCall{0x43481eabu, &bindings, initialize_esp},
                           NativeInitializerCall{0x4347f6dbu, &bindings, initialize_blur},
                           NativeInitializerCall{0x43484e4eu, &bindings, initialize_console},
                           NativeInitializerCall{0x4347da60u, &bindings, initialize_fov},
                           NativeInitializerCall{0x43481f44u, &bindings, initialize_purchase_log}};
    for (const auto& call : calls)
        sources.bind_initializer(call);
}
} //namespace skeet::client
