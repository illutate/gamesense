#include "skeet/effects/owner_lifecycle.hpp"
#include "skeet/effects/owner_settings.hpp"
#include "skeet/effects/model_hooks.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/startup_convars.hpp"
#include <array>
#include <cstring>
#include <new>
#include <string>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define EFFECT_MEMBER __thiscall
#define EFFECT_FAST __fastcall
#else
#define EFFECT_MEMBER
#define EFFECT_FAST
#endif
using OwnerEntry = void(EFFECT_MEMBER*)(void*);
struct EffectsInstance;
struct HudEndpoint : hud::HudFrameCallback {
    EffectsInstance* owner;
};
struct FrameEndpoint : engine::FrameUpdateEndpoint {
    EffectsInstance* owner;
};

void EFFECT_MEMBER destroy(void*);
void EFFECT_MEMBER unchanged(void*) {} //433CCE8E
void EFFECT_MEMBER resources(void*);
void EFFECT_MEMBER initialize(void*);
void EFFECT_MEMBER shutdown(void*);
void EFFECT_MEMBER reset(void*);
void EFFECT_MEMBER reset_forwarder(void*);
void EFFECT_MEMBER clear_marker(void*);
void EFFECT_FAST hud_entry(void*, ui::RenderQueue*, void*, std::int32_t*);
void EFFECT_FAST frame_entry(void*, std::uint8_t);
const std::array<OwnerEntry, 9> owner_table{destroy,    unchanged,       resources,
                                            initialize, shutdown,        reset,
                                            unchanged,  reset_forwarder, clear_marker};
const std::array<hud::HudFrameEntry, 1> hud_table{hud_entry};
const std::array<engine::FrameUpdateEntry, 1> frame_table{frame_entry};

struct EffectsInstance {
    const OwnerEntry* vtable = nullptr;
    client::NativeCommandPhaseReceiver command_receiver;
    HudEndpoint hud{{nullptr}, this};
    client::NativeEntityObserverReceiver entity_receiver;
    WorldUpdateObserver updates;
    FrameEndpoint frame{{nullptr}, this};
    EffectsOwnerBindings bindings;
    engine::JobMemory memory;
    events::ServerImpactBindings impact_bindings;
    events::ServerImpactRuntime impacts;
    std::uint8_t impact_listener_enabled = 0; //native full+74C
    events::EventListener impact_listener;

    static void impact(void* object, events::GameEventReader& event,
                       events::EventPlayerIndices& players) {
        events::process_server_impact(static_cast<EffectsInstance*>(object)->impacts, event,
                                      players);
    }
    EffectsInstance(const EffectsOwnerRegistries& registry, ConfigRegistries& config,
                    EffectsOwnerBindings context)
        : updates(context.updates), bindings(context), memory(registry.lifecycle.memory),
          impact_bindings{context.command.bindings.debug_overlay, context.command.world.suspended},
          impacts{context.command.world.settings, impact_bindings},
          impact_listener{events::bullet_impact_hash, this, impact} {
        bind_model_material_owner(bindings.active);
        //4348E6FA..4348E72A. Every endpoint joins its actual list before the
        // native table-publication block; no list is sorted or reconstructed.
        registry.lifecycle.append(this);
        registry.commands.append(command_receiver, &bindings.command); //4348E702,+8
        hud::register_hud_frame(registry.hud, &hud);
        registry.entities.append_effects(entity_receiver, bindings.command); //4348E712,+18
        network::register_update_observer(registry.updates, updates.endpoint);
        engine::register_frame_update(registry.frames, &frame);
        registry.empty_ticks.append(bindings.command); //4348E72A
        updates.initialize_vtable(); //4348E733
        frame.vtable = frame_table.data(); //4348E73F
        vtable = owner_table.data(); //4348E74B
        registry.commands.publish(command_receiver,
                                  client::CommandOwnerId::owner_434700e4); //4348E751
        hud.vtable = hud_table.data(); //4348E758
        registry.entities.publish_effects(entity_receiver); //4348E75F
        registry.empty_ticks.publish(bindings.command); //4348E766

        auto& command = bindings.command;
        auto& world = command.world;
        auto& models = command.models;
        auto& callbacks = bindings.updates;
        callbacks.state.second_hook_installed = 0;
        models.local_alpha = 1.0f;
        models.local_alpha_overridden = false;
        initialize_model_effects_owner_settings(config, world.settings, bindings.lifecycle.colors);
        bindings.lifecycle.state.field_450 = 0;
        callbacks.state.camera_pending = 0;
        world.material_flags = 0;
        models.solid_material = models.shadow_material = nullptr;
        models.material_name_hashes = {0xF4A7A78Au, 0x27DB215Fu};
        models.base_texture = models.metallic_environment = models.glow_environment =
            models.fullscreen_texture = nullptr;
        models.texture_names = {"vgui/white", "editor/cube_vertigo_blurred_hdr",
                                "models/effects/cube_white", "_rt_Fullscreen"};
        world.post_processing.saved = 0;
        world.post_processing.active = false;
        world.cascaded_shadows.saved = 0;
        world.cascaded_shadows.active = false;
        for (auto& variable : world.sky) {
            variable.saved = 0;
            variable.active = false;
        }
        world.previous_wall_alpha = 1.0f;
        world.previous_prop_transparency = world.previous_prop_transparency_2 = 100;
        world.previous_night_rgb = 0;
        callbacks.state.sky_name = "sky_csgo_night02";
        callbacks.state.saved_sky_name = nullptr;
        bindings.lifecycle.state.allocation_760 = nullptr;
        //4348F055..F0AF are native owner captures of the then-current genuine
        //ConVars. Keep the global encoded cells authoritative until this cut.
        auto& data = bindings.data;
        const auto variable = [&](std::uint32_t cell, std::uint32_t key) {
            return engine::NativeConVarReference(data.native_cell<std::uint32_t>(cell), key).get();
        };
        world.post_processing.variable = variable(0x43468F44u, 0x684EE759u);
        world.cascaded_shadows.variable = variable(0x43468F98u, 0xADB120F0u);
        world.sky[0].variable = variable(0x4346A37Cu, 0x38DF1D5Eu);
        world.sky[1].variable = variable(0x4346925Cu, 0x89E7CF90u);
        world.sky[2].variable = variable(0x434693A8u, 0x27F664A6u);
        world.sky[3].variable = variable(0x43468A94u, 0x24CE2F36u);
        auto& classification = bindings.classification;
        classification.renderable_vtables[6] = data.native_cell<void*>(0x4346A81Cu); //4348F0B5..BA
        auto* attachment = data.native_cell<void*>(0x43468D28u); //4348F0C0
        data.native_cell<void*>(0x4346A81Cu) = nullptr; //4348F0C5, consumed only here
        classification.renderable_vtables[4] = attachment; //4348F0FB
    }
    void set_impact_listener(std::uint8_t value) {
        if (value == impact_listener_enabled)
            return;
        impact_listener_enabled = value;
        if (value)
            bindings.event_registry.add(impact_listener);
        else
            bindings.event_registry.remove(impact_listener);
    }
    void release_storage() { //whole433D6BCF, in its original reverse order
        std::string().swap(bindings.updates.state.sky_name);
        set_impact_listener(0);
        auto& models = bindings.command.models;
        std::array<void**, 4> textures{&models.base_texture, &models.metallic_environment,
                                       &models.glow_environment, &models.fullscreen_texture};
        for (std::size_t i = textures.size(); i != 0; --i) {
            auto& texture = *textures[i - 1];
            if (texture && models.interfaces.material_system) {
                engine::invoke<void>(texture, 11);
                texture = nullptr;
            }
            std::string().swap(models.texture_names[i - 1]);
        }
        for (auto* reference :
             std::array<void**, 2>{&models.shadow_material, &models.solid_material})
            if (*reference && models.interfaces.material_system) {
                engine::invoke<void>(*reference, 13);
                *reference = nullptr;
            }
    }
};
void EFFECT_MEMBER destroy(void* object) { //433279E3 ->433D6BCF ->433837D0
    auto* owner = static_cast<EffectsInstance*>(object);
    owner->bindings.active = nullptr;
    owner->release_storage();
    const auto memory = owner->memory;
    owner->~EffectsInstance();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void EFFECT_MEMBER resources(void* object) {
    static_cast<EffectsInstance*>(object)->bindings.command.models.initialize_resources();
}
void EFFECT_MEMBER initialize(void* object) {
    auto& bindings = static_cast<EffectsInstance*>(object)->bindings;
    bindings.hooks.bind_replacements();
    bindings.lifecycle.initialize_bindings();
}
void EFFECT_MEMBER shutdown(void* object) {
    static_cast<EffectsInstance*>(object)->bindings.lifecycle.shutdown();
}
void EFFECT_MEMBER reset(void* object) {
    static_cast<EffectsInstance*>(object)->bindings.lifecycle.reset();
}
void EFFECT_MEMBER reset_forwarder(void* object) {
    static_cast<EffectsInstance*>(object)->vtable[5](object);
}
void EFFECT_MEMBER clear_marker(void* object) {
    static_cast<EffectsInstance*>(object)->bindings.lifecycle.clear_shot_marker();
}
void EFFECT_FAST hud_entry(void* object, ui::RenderQueue*, void*, std::int32_t*) {
    static_cast<HudEndpoint*>(static_cast<hud::HudFrameCallback*>(object))
        ->owner->bindings.command.world.update();
}
void EFFECT_FAST frame_entry(void* object, std::uint8_t refresh) { //433C8C2A
    if (!refresh)
        return;
    auto* owner =
        static_cast<FrameEndpoint*>(static_cast<engine::FrameUpdateEndpoint*>(object))->owner;
    owner->set_impact_listener(owner->bindings.command.world.settings.bullet_impacts.value);
}
#undef EFFECT_MEMBER
#undef EFFECT_FAST
}
std::size_t model_effects_owner_size() noexcept {
    return sizeof(EffectsInstance);
}
void construct_model_effects_owner(void* storage, const EffectsOwnerRegistries& registry,
                                   ConfigRegistries& config, EffectsOwnerBindings bindings) {
    ::new (storage) EffectsInstance(registry, config, bindings);
    bindings.active = &bindings.command;
}
} // namespace skeet::effects
