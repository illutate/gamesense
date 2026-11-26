#include "skeet/engine/build_hook_initialization.hpp"
#include "skeet/engine/player_hook_family.hpp"
#include "skeet/animation/fix_animations.hpp"
#include "skeet/animation/remote_setup_bones.hpp"
#include "skeet/effects/entity_visibility.hpp"
#include "skeet/effects/harmless_molotov.hpp"
#include "skeet/effects/model_hooks.hpp"
#include "skeet/misc/ping_spike_channel.hpp"
#include "skeet/misc/network_console_handlers.hpp"
#include "skeet/misc/viewmodel_hooks.hpp"
#include "skeet/misc/auto_accept_owner.hpp"
#include "skeet/network/client_state_hook_family.hpp"
#include "skeet/network/packet_dispatch.hpp"
#include "skeet/script/client_api.hpp"
#include <array>
#include <stdexcept>

namespace skeet::engine {
namespace {
// Build-specific recipe data independently identified in contexts1792..2135.
// Module cells refer to the canonical source data owner. RTTI is resolved from
// those actual loaded modules; no saved original-table pointer is used.
constexpr std::array<BuildHookDescriptorRecipe, 26> descriptors{{
    {BuildHookDescriptor::channel, 0x43475B04, 0x43468D20, 0x81967230, 0x00000000, 0x7FC, 0xB0C,
     4}, // CNetChan
    {BuildHookDescriptor::local_player, 0x43475954, 0x4346A55C, 0x1B1A0D29, 0x00000000, 0x1110,
     0x6D0, 22}, // C_CSPlayer
    {BuildHookDescriptor::local_thinkable, 0x43475878, 0x4346A55C, 0x1B1A0D29, 0x3C81EC5C, 0x37C,
     0x8B8, 2}, // C_CSPlayer / IClientThinkable
    {BuildHookDescriptor::local_renderable, 0x43475998, 0x4346A55C, 0x1B1A0D29, 0xC494EC7A, 0x894,
     0x2F8, 1}, // C_CSPlayer / IClientRenderable
    {BuildHookDescriptor::local_networkable, 0x43475B48, 0x4346A55C, 0x1B1A0D29, 0xA04AE66E, 0xF9C,
     0x348, 4}, // C_CSPlayer / IClientNetworkable
    {BuildHookDescriptor::remote_player, 0x43475830, 0x4346A55C, 0x1B1A0D29, 0x00000000, 0x1248,
     0xF00, 10}, // C_CSPlayer
    {BuildHookDescriptor::remote_thinkable, 0x43475B60, 0x4346A55C, 0x1B1A0D29, 0x3C81EC5C, 0x7CC,
     0xB2C, 2}, // C_CSPlayer / IClientThinkable
    {BuildHookDescriptor::remote_renderable, 0x4347589C, 0x4346A55C, 0x1B1A0D29, 0xC494EC7A, 0x1270,
     0x124C, 2}, // C_CSPlayer / IClientRenderable
    {BuildHookDescriptor::remote_networkable, 0x43475A28, 0x4346A55C, 0x1B1A0D29, 0xA04AE66E,
     0x1140, 0x318, 4}, // C_CSPlayer / IClientNetworkable
    {BuildHookDescriptor::game_event_listener, 0x43475864, 0x4346A55C, 0x1B1A0D29, 0xC7BCB2A1,
     0xEE4, 0xFB8, 1}, // C_CSPlayer / CGameEventListener
    {BuildHookDescriptor::ragdoll_networkable, 0x43475AEC, 0x4346A55C, 0x17843051, 0xA04AE66E,
     0x79C, 0x380, 2}, // C_CSRagdoll / IClientNetworkable
    {BuildHookDescriptor::fire_bullets_networkable, 0x43475A68, 0x4346A55C, 0xB2595DD8, 0xA04AE66E,
     0x10F0, 0x340, 1}, // C_TEFireBullets / IClientNetworkable
    {BuildHookDescriptor::molotov, 0x43475A08, 0x4346A55C, 0x0C2ECE3E, 0x00000000, 0x1128, 0x8D8,
     2}, // C_MolotovGrenade
    {BuildHookDescriptor::incendiary, 0x43475B18, 0x4346A55C, 0x4316B52C, 0x00000000, 0x444, 0x108C,
     2}, // C_IncendiaryGrenade
    {BuildHookDescriptor::viewmodel, 0x43475B34, 0x4346A55C, 0x17CEECD0, 0x00000000, 0x6C8, 0x448,
     6}, // C_PredictedViewModel
    {BuildHookDescriptor::inferno, 0x43475AC4, 0x4346A55C, 0x40D7A20F, 0x00000000, 0xFB4, 0x10B4,
     2}, // C_Inferno
    {BuildHookDescriptor::particle_effect, 0x43475908, 0x4346A55C, 0xE6C657EA, 0x043BBC72, 0x10B0,
     0x122C, 1}, // CNewParticleEffect / IParticleEffect
    {BuildHookDescriptor::particle_unknown, 0x434758F4, 0x4346A55C, 0xE6C657EA, 0x4A0A8D9A, 0x8FC,
     0xB64, 1}, // CNewParticleEffect / IClientUnknown
    {BuildHookDescriptor::particle_renderable, 0x434757F4, 0x4346A55C, 0xE6C657EA, 0xC494EC7A,
     0xEFC, 0x338, 1}, // CNewParticleEffect / IClientRenderable
    {BuildHookDescriptor::focus_overlay, 0x434757C8, 0x43468D20, 0xBA6192DD, 0x00000000, 0x10D8,
     0x10F4, 1}, // CFocusOverlayPanel
    {BuildHookDescriptor::client_state, 0x43475A3C, 0x43468D20, 0x747CA973, 0x00000000, 0x8B4,
     0x818, 13}, // CClientState
    {BuildHookDescriptor::studio, 0x434757B0, 0x4346A650, 0x9223010A, 0x00000000, 0x7B4, 0xB3C,
     5}, // CStudioRenderContext
    {BuildHookDescriptor::leaf_system, 0x434757E0, 0x4346A55C, 0x9D79E359, 0x00000000, 0x814, 0xF50,
     1}, // CClientLeafSystem
    {BuildHookDescriptor::auto_accept, 0x43475814, 0x43469234, 0xF8B3A903, 0x00000000, 0x914, 0x8C8,
     2}, // panorama::CUISoundSystemSource2
    {BuildHookDescriptor::console, 0x434758B8, 0x4346A55C, 0x46E07364, 0x00000000, 0x1088, 0x478,
     1}, // CGameConsoleDialog
    {BuildHookDescriptor::overlay_stream, 0x4347591C, 0x43468D08, 0xF0EC2C9D, 0x00000000, 0x314,
     0x898, 1}, // CSharedMemStream
}};
constexpr std::array<BuildHookNativeReplacement, 4> channel_entries{{
    {22, 0x43416B44, 0xB10},
    {39, 0x4336C1B7, 0xB18},
    {46, 0x433204DC, 0xB20},
    {40, 0x433877FF, 0xB28},
}};
constexpr std::array<BuildHookNativeReplacement, 2> local_thinkable_entries{{
    {1, 0x433FD3BF, 0x8BC},
    {4, 0x43367D1D, 0x8C4},
}};
constexpr std::array<BuildHookNativeReplacement, 1> local_renderable_entries{{
    {13, 0x433ABC77, 0x2FC},
}};
constexpr std::array<BuildHookNativeReplacement, 4> local_networkable_entries{{
    {1, 0x43403563, 0x34C},
    {6, 0x433E129A, 0x354},
    {7, 0x433B7093, 0x35C},
    {13, 0x433836C8, 0x364},
}};
constexpr std::array<BuildHookNativeReplacement, 2> remote_thinkable_entries{{
    {1, 0x43383CA2, 0xB30},
    {4, 0x43367D1D, 0xB38},
}};
constexpr std::array<BuildHookNativeReplacement, 2> remote_renderable_entries{{
    {13, 0x4334AD2A, 0x1250},
    {37, 0x4336F76D, 0x1258},
}};
constexpr std::array<BuildHookNativeReplacement, 4> remote_networkable_entries{{
    {1, 0x433FAA6D, 0x31C},
    {3, 0x4339CFA8, 0x324},
    {6, 0x433E129A, 0x32C},
    {7, 0x4337DE0B, 0x334},
}};
constexpr std::array<BuildHookNativeReplacement, 1> game_event_listener_entries{{
    {1, 0x433E9471, 0xFBC},
}};
constexpr std::array<BuildHookNativeReplacement, 2> ragdoll_networkable_entries{{
    {1, 0x4340C3A8, 0x384},
    {5, 0x433990E9, 0x38C},
}};
constexpr std::array<BuildHookNativeReplacement, 1> fire_bullets_networkable_entries{{
    {7, 0x433B06F0, 0x344},
}};
constexpr std::array<BuildHookNativeReplacement, 2> molotov_entries{{
    {0, 0x43402028, 0x8DC},
    {98, 0x4334ADEE, 0x8E4},
}};
constexpr std::array<BuildHookNativeReplacement, 2> incendiary_entries{{
    {0, 0x43402028, 0x1090},
    {98, 0x4334ADEE, 0x1098},
}};
constexpr std::array<BuildHookNativeReplacement, 2> inferno_entries{{
    {0, 0x43402028, 0x10B8},
    {110, 0x433CEAAB, 0x10C0},
}};
constexpr std::array<BuildHookNativeReplacement, 1> particle_effect_entries{{
    {0, 0x433E7C16, 0x1230},
}};
constexpr std::array<BuildHookNativeReplacement, 1> particle_unknown_entries{{
    {0, 0x4340B2C6, 0xB68},
}};
constexpr std::array<BuildHookNativeReplacement, 1> particle_renderable_entries{{
    {9, 0x433C8A72, 0x33C},
}};
constexpr std::array<BuildHookNativeReplacement, 1> focus_overlay_entries{{
    {131, 0x43416A0F, 0x10F8},
}};
constexpr std::array<BuildHookNativeReplacement, 1> overlay_stream_entries{{
    {2, 0x4341F338, 0x89C},
}};
std::span<const BuildHookNativeReplacement> secondary_entries(BuildHookDescriptor kind) {
    switch (kind) {
    case BuildHookDescriptor::channel:
        return channel_entries;
    case BuildHookDescriptor::local_thinkable:
        return local_thinkable_entries;
    case BuildHookDescriptor::local_renderable:
        return local_renderable_entries;
    case BuildHookDescriptor::local_networkable:
        return local_networkable_entries;
    case BuildHookDescriptor::remote_thinkable:
        return remote_thinkable_entries;
    case BuildHookDescriptor::remote_renderable:
        return remote_renderable_entries;
    case BuildHookDescriptor::remote_networkable:
        return remote_networkable_entries;
    case BuildHookDescriptor::game_event_listener:
        return game_event_listener_entries;
    case BuildHookDescriptor::ragdoll_networkable:
        return ragdoll_networkable_entries;
    case BuildHookDescriptor::fire_bullets_networkable:
        return fire_bullets_networkable_entries;
    case BuildHookDescriptor::molotov:
        return molotov_entries;
    case BuildHookDescriptor::incendiary:
        return incendiary_entries;
    case BuildHookDescriptor::inferno:
        return inferno_entries;
    case BuildHookDescriptor::particle_effect:
        return particle_effect_entries;
    case BuildHookDescriptor::particle_unknown:
        return particle_unknown_entries;
    case BuildHookDescriptor::particle_renderable:
        return particle_renderable_entries;
    case BuildHookDescriptor::focus_overlay:
        return focus_overlay_entries;
    case BuildHookDescriptor::overlay_stream:
        return overlay_stream_entries;
    default:
        throw std::invalid_argument("Descriptor has an existing whole-family manifest");
    }
}
void bind_secondary(const BuildHookDescriptorRecipe& recipe, VtableHookMetadata& metadata,
                    const BuildHookFamilies* f) {
    for (const auto& entry : secondary_entries(recipe.kind)) {
        switch (entry.native_entry) {
        case 0x433FD3BF:
        case 0x43383CA2:
        case 0x433E129A:
        case 0x433B7093:
        case 0x4337DE0B:
            if (f)
                f->animations.bind_native_replacement(metadata, entry.slot, entry.native_entry);
            else
                metadata.replacement_table[entry.slot] =
                    animation::FixAnimationsRuntime::native_replacement_entry(entry.slot,
                                                                              entry.native_entry);
            break;
        case 0x433ABC77: // MOV EDX,EBP; JMP433E80F7. The body ignores incoming EDX.
            if (f)
                f->animations.bind_native_replacement(metadata, entry.slot, 0x433E80F7);
            else
                metadata.replacement_table[entry.slot] =
                    animation::FixAnimationsRuntime::native_replacement_entry(entry.slot,
                                                                              0x433E80F7);
            break;
        case 0x4334AD2A:
            if (f)
                f->remote_bones.bind(metadata);
            else
                metadata.replacement_table[entry.slot] =
                    animation::RemoteSetupBonesRuntime::source_entry();
            break;
        case 0x433204DC:
            if (f)
                misc::bind_ping_spike_channel_runtime(f->channel);
            metadata.replacement_table[entry.slot] =
                reinterpret_cast<void*>(&misc::ping_spike_send_datagram_hook);
            break;
        case 0x433877FF:
            if (f)
                misc::bind_network_message_runtime(f->messages);
            metadata.replacement_table[entry.slot] =
                reinterpret_cast<void*>(&misc::send_network_message_hook);
            break;
        case 0x43402028:
            metadata.replacement_table[entry.slot] =
                reinterpret_cast<void*>(&misc::viewmodel_destroy_hook);
            break;
        default:
            if (f)
                f->callbacks.bind(entry.native_entry, metadata, entry.slot);
            else
                BuildHookCallbacks::bind_static(entry.native_entry, metadata, entry.slot);
            break;
        }
    }
}
template <class Entries>
void copy_static_entries(VtableHookMetadata& metadata, const Entries& entries) {
    for (const auto& entry : entries) {
        if constexpr (requires { entry.native.slot; })
            metadata.replacement_table[entry.native.slot] = entry.source_entry;
        else
            metadata.replacement_table[entry.slot] = entry.source_entry;
    }
}
void bind_descriptor_entries(const BuildHookDescriptorRecipe& recipe, VtableHookMetadata& metadata,
                             const BuildHookFamilies* f) {
    switch (recipe.kind) {
    case BuildHookDescriptor::local_player:
        if (f)
            f->players.bind_replacements(PlayerPrimaryKind::local);
        else
            bind_player_primary_static_entries(metadata, PlayerPrimaryKind::local);
        break;
    case BuildHookDescriptor::remote_player:
        if (f)
            f->players.bind_replacements(PlayerPrimaryKind::remote);
        else
            bind_player_primary_static_entries(metadata, PlayerPrimaryKind::remote);
        break;
    case BuildHookDescriptor::viewmodel:
        if (f)
            f->viewmodels.bind_replacements();
        else
            copy_static_entries(metadata, misc::viewmodel_hook_replacements());
        break;
    case BuildHookDescriptor::client_state:
        if (f)
            f->client_state.bind_replacements();
        else
            copy_static_entries(metadata, network::client_state_source_replacements());
        break;
    case BuildHookDescriptor::studio:
        if (f)
            f->models.bind_studio_replacements();
        else
            copy_static_entries(metadata, effects::studio_model_hook_replacements());
        break;
    case BuildHookDescriptor::leaf_system:
        if (f)
            f->models.bind_occlusion_replacements();
        else
            copy_static_entries(metadata, effects::model_occlusion_hook_replacements());
        break;
    case BuildHookDescriptor::auto_accept:
        if (f)
            f->auto_accept.bind_replacements();
        else
            copy_static_entries(metadata, misc::auto_accept_hook_replacements());
        break;
    case BuildHookDescriptor::console:
        metadata.replacement_table[279] = reinterpret_cast<void*>(&script::console_input_hook);
        break;
    case BuildHookDescriptor::ragdoll_networkable:
        if (f)
            f->ragdolls.bind_networkable_table(metadata);
        else
            effects::EntityNetworkableRuntime::bind_static_networkable_table(metadata);
        break;
    case BuildHookDescriptor::fire_bullets_networkable:
        if (f)
            network::bind_packet_dispatch_runtime(f->packets);
        metadata.replacement_table[7] = reinterpret_cast<void*>(&network::packet_update_hook);
        break;
    case BuildHookDescriptor::inferno:
        metadata.replacement_table[0] = reinterpret_cast<void*>(&misc::viewmodel_destroy_hook);
        if (f)
            f->particles.bind_table(effects::HarmlessMolotovHook::creation, metadata);
        else
            effects::HarmlessMolotovRuntime::bind_static_table(
                effects::HarmlessMolotovHook::creation, metadata);
        break;
    case BuildHookDescriptor::particle_effect:
        if (f)
            f->particles.bind_table(effects::HarmlessMolotovHook::primary, metadata);
        else
            effects::HarmlessMolotovRuntime::bind_static_table(
                effects::HarmlessMolotovHook::primary, metadata);
        break;
    case BuildHookDescriptor::particle_unknown:
        if (f)
            f->particles.bind_table(effects::HarmlessMolotovHook::secondary, metadata);
        else
            effects::HarmlessMolotovRuntime::bind_static_table(
                effects::HarmlessMolotovHook::secondary, metadata);
        break;
    case BuildHookDescriptor::particle_renderable:
        if (f)
            f->particles.bind_table(effects::HarmlessMolotovHook::renderable, metadata);
        else
            effects::HarmlessMolotovRuntime::bind_static_table(
                effects::HarmlessMolotovHook::renderable, metadata);
        break;
    default:
        bind_secondary(recipe, metadata, f);
        break;
    }
}
}
std::span<const BuildHookDescriptorRecipe, 26> build_hook_descriptors() noexcept {
    return descriptors;
}
std::vector<BuildHookNativeReplacement> build_hook_native_replacements(BuildHookDescriptor kind) {
    std::vector<BuildHookNativeReplacement> result;
    const auto append = [&](const auto& entries, std::uint32_t operand) {
        for (const auto& e : entries) {
            result.push_back({e.slot, e.native_entry, operand + 4});
            operand += 8;
        }
    };
    switch (kind) {
    case BuildHookDescriptor::local_player:
        append(local_player_native_replacements, local_player_replacement_stream);
        break;
    case BuildHookDescriptor::remote_player:
        append(remote_player_native_replacements, remote_player_replacement_stream);
        break;
    case BuildHookDescriptor::viewmodel:
        append(misc::viewmodel_hook_replacements(), misc::viewmodel_hook_replacements_operand);
        break;
    case BuildHookDescriptor::client_state:
        append(network::client_state_native_replacements, network::client_state_replacement_stream);
        break;
    case BuildHookDescriptor::studio:
        append(effects::studio_model_hook_replacements(), 0xB3C);
        break;
    case BuildHookDescriptor::leaf_system:
        append(effects::model_occlusion_hook_replacements(), 0xF50);
        break;
    case BuildHookDescriptor::auto_accept:
        append(misc::auto_accept_hook_replacements(), misc::auto_accept_hook_replacements_operand);
        break;
    case BuildHookDescriptor::console:
        result.push_back({279, 0x4334E722, 0x47C});
        break;
    default: {
        const auto entries = secondary_entries(kind);
        result.assign(entries.begin(), entries.end());
        break;
    }
    }
    return result;
}

namespace {
void validate_families(HookDescriptorStorage& storage, VtableHookRegistry& hooks,
                       const BuildHookFamilies& f) {
    if (&f.players.metadata(PlayerPrimaryKind::local) !=
            &storage.at(local_player_descriptor_identity) ||
        &f.players.metadata(PlayerPrimaryKind::remote) !=
            &storage.at(remote_player_descriptor_identity) ||
        &f.client_state.metadata() != &storage.at(network::client_state_descriptor_identity) ||
        &f.auto_accept.metadata() != &storage.at(misc::auto_accept_hook_descriptor) ||
        &f.console.metadata != &storage.at(0x434758B8) ||
        &f.viewmodels.image() != &storage.image() ||
        static_cast<void*>(&f.models.studio_descriptor()) !=
            &storage.at(effects::studio_model_hook_descriptor) ||
        static_cast<void*>(&f.models.occlusion_descriptor()) !=
            &storage.at(effects::model_occlusion_hook_descriptor) ||
        &f.particles.bindings.hook_registry != &hooks ||
        f.particles.bindings.entity_hooks[0] != &storage.at(0x43475908) ||
        f.particles.bindings.entity_hooks[1] != &storage.at(0x434758F4) ||
        f.particles.bindings.entity_hooks[2] != &storage.at(0x434757F4))
        throw std::invalid_argument(
            "Build hook families must share canonical descriptors and cleanup storage");
}
}
void prepare_build_hook_descriptors(HookDescriptorStorage& storage, JobMemory memory,
                                    NativeImageEnvironment images, HookDescriptorSystem system) {
    for (const auto& recipe : descriptors) {
        auto& metadata = storage.at(recipe.descriptor);
        prepare_rtti_vtable_hook(metadata,
                                 storage.image().native_cell<std::byte*>(recipe.module_cell),
                                 recipe.type_hash, recipe.base_hash, images, memory, system);
        // The complete original26-group phase terminates12 when the Steam
        // module is absent: it finishes25groups/93entries, reads the overlay
        // descriptor words, then fails before its replacement operands.
        // Helper-local null handling does not make that final group optional.
        if (!metadata.replacement_table)
            throw std::runtime_error("Required build hook RTTI table or allocation is unavailable");
        bind_descriptor_entries(recipe, metadata, nullptr);
    }
}
void bind_build_hook_runtime_contexts(HookDescriptorStorage& storage, VtableHookRegistry& hooks,
                                      const BuildHookFamilies& families) {
    validate_families(storage, hooks, families);
    for (const auto& recipe : descriptors) {
        auto& metadata = storage.at(recipe.descriptor);
        if (!metadata.replacement_table)
            throw std::logic_error("Early build hook descriptor phase has not completed");
        bind_descriptor_entries(recipe, metadata, &families);
    }
}
void initialize_build_hooks(HookDescriptorStorage& storage, VtableHookRegistry& hooks,
                            NativeImageEnvironment images, HookDescriptorSystem system,
                            const BuildHookFamilies& families) {
    validate_families(storage, hooks, families);
    prepare_build_hook_descriptors(storage, hooks.memory, images, system);
    bind_build_hook_runtime_contexts(storage, hooks, families);
}
}
