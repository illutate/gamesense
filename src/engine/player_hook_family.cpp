#include "skeet/engine/player_hook_family.hpp"
#include "skeet/animation/fix_animations.hpp"
#include "skeet/client/command_orchestrator.hpp"
#include "skeet/client/local_player_animation_hooks.hpp"
#include "skeet/client/player_command_hooks.hpp"
#include "skeet/client/player_command_queries.hpp"
#include "skeet/client/player_physics_hooks.hpp"
#include "skeet/effects/entity_visibility.hpp"
#include "skeet/effects/free_look_capture.hpp"
#include "skeet/prediction/player_simulation.hpp"
#include <stdexcept>

namespace skeet::engine {
namespace {
const PlayerPrimaryHookRuntimes* active_runtimes = nullptr;

#if defined(_M_IX86) || defined(__i386__)
#define SKEET_PLAYER_ENTRY __fastcall
#else
#define SKEET_PLAYER_ENTRY
#endif

// Native433EB3F4 receives only ECX. The shared production body resolves the
// current metadata at its original155 call, after all preceding observers.
void SKEET_PLAYER_ENTRY player_simulation_entry(void* player, void*) {
    prediction::simulate_hooked_player_command(player, active_runtimes->simulation);
}

// Native433C82DC receives three raw stack arguments and returns RET12. Its
// update registry is the very same43475988 vector used by FixAnimations.
void SKEET_PLAYER_ENTRY player_view_entry(void* player, void*, std::uintptr_t first,
                                          std::uintptr_t second, std::uintptr_t third) {
    const auto& bindings = active_runtimes->animation_updates.bindings;
    bindings.view.calculate_hooked_view(player, bindings.duck,
                                        active_runtimes->animations.bindings.update_observers,
                                        first, second, third);
}

// Native433CCEA1 calls the original337 first, before its camera/trace work.
void SKEET_PLAYER_ENTRY player_free_look_entry(void* player, void*, void* setup) {
    const auto table = *static_cast<void***>(player);
    const auto* metadata = static_cast<const VtableHookMetadata*>(table[-2]);
    effects::capture_free_look_view_hook(player, metadata->original_table, setup,
                                         active_runtimes->free_look);
}

#undef SKEET_PLAYER_ENTRY
}

std::span<const PlayerPrimaryNativeReplacement>
player_primary_native_replacements(PlayerPrimaryKind kind) {
    switch (kind) {
    case PlayerPrimaryKind::local:
        return local_player_native_replacements;
    case PlayerPrimaryKind::remote:
        return remote_player_native_replacements;
    }
    throw std::invalid_argument("Unknown primary player hook family");
}

PlayerHookFamily::PlayerHookFamily(VtableHookMetadata& local, VtableHookMetadata& remote,
                                   VtableHookRegistry& hooks, std::byte* const& client_module,
                                   NativeImageEnvironment images, HookDescriptorSystem system,
                                   PlayerPrimaryHookRuntimes runtimes) noexcept
    : local_(local), remote_(remote), hooks_(hooks), client_module_(client_module), images_(images),
      system_(system), runtimes_(runtimes) {}

VtableHookMetadata& PlayerHookFamily::metadata(PlayerPrimaryKind kind) const {
    switch (kind) {
    case PlayerPrimaryKind::local:
        return local_;
    case PlayerPrimaryKind::remote:
        return remote_;
    }
    throw std::invalid_argument("Unknown primary player hook family");
}

void PlayerHookFamily::prepare(PlayerPrimaryKind kind) {
    prepare_rtti_vtable_hook(metadata(kind), client_module_, player_primary_rtti_hash,
                             player_primary_rtti_base_hash, images_, hooks_.memory, system_);
    bind_replacements(kind);
}

namespace {
void bind_primary_entries(VtableHookMetadata& descriptor, PlayerPrimaryKind kind,
                          const PlayerPrimaryHookRuntimes* context) {
    for (const auto& replacement : player_primary_native_replacements(kind)) {
        switch (replacement.native_entry) {
        case 0x433842c1: //local78
        case 0x4334d3eb: //local88, hidden caller ESI
        case 0x4340191d: //local113
        case 0x433e97c1: //local170
            if (context)
                context->queries.bind_native_replacement(descriptor, replacement.slot,
                                                         replacement.native_entry);
            else
                descriptor.replacement_table[replacement.slot] =
                    client::PlayerCommandQueries::native_replacement_entry(
                        replacement.slot, replacement.native_entry);
            break;
        case 0x43402307: //shared109
        case 0x4333181e: //shared145
        case 0x4333c562: //shared173
        case 0x433abbf0: //remote78
        case 0x433875be: //remote113
        case 0x43382222: //remote169
            if (context)
                context->physics.bind_native_replacement(descriptor, replacement.slot,
                                                         replacement.native_entry);
            else
                descriptor.replacement_table[replacement.slot] =
                    client::PlayerPhysicsHooks::native_replacement_entry(replacement.slot,
                                                                         replacement.native_entry);
            break;
        case 0x43330f73: //190
        case 0x4335c639: //207
        case 0x43388201: //198
        case 0x4335b798: //local277; not present in the remote recipe
            if (context)
                context->animations.bind_native_replacement(descriptor, replacement.slot,
                                                            replacement.native_entry);
            else
                descriptor.replacement_table[replacement.slot] =
                    animation::FixAnimationsRuntime::native_replacement_entry(
                        replacement.slot, replacement.native_entry);
            break;
        case 0x43341fa7: //local222
        case 0x43329436: //local224
        case 0x433c6689: //local268, hidden caller EBP
        case 0x433e009a: //local294, hidden caller EDI
            if (context)
                context->animation_updates.bind_native_replacement(descriptor, replacement.slot,
                                                                   replacement.native_entry);
            else
                descriptor.replacement_table[replacement.slot] =
                    client::LocalPlayerAnimationHooks::native_replacement_entry(
                        replacement.slot, replacement.native_entry);
            break;
        case 0x4331e412: //local230
        case 0x433ab4b6: //local298
        case 0x433d18b0: //local350
            if (context)
                context->command_hooks.bind_native_replacement(descriptor, replacement.slot,
                                                               replacement.native_entry);
            else
                descriptor.replacement_table[replacement.slot] =
                    client::PlayerCommandHooks::native_replacement_entry(replacement.slot,
                                                                         replacement.native_entry);
            break;
        case 0x433eb3f4:
            descriptor.replacement_table[replacement.slot] =
                reinterpret_cast<void*>(&player_simulation_entry);
            break;
        case 0x4334f53d:
            if (context)
                client::bind_create_move_runtime(context->commands);
#if defined(_M_IX86) || defined(__i386__)
            // The captured outer wrapper hands its saved EBX address to the
            // inner master. The engine's incoming EDX is not that address.
            descriptor.replacement_table[replacement.slot] =
                reinterpret_cast<void*>(&client::player_create_move_hook);
#else
            throw std::logic_error("The saved-EBX player CreateMove hook requires x86");
#endif
            break;
        case 0x433c82dc:
            descriptor.replacement_table[replacement.slot] =
                reinterpret_cast<void*>(&player_view_entry);
            break;
        case 0x433ccea1:
            descriptor.replacement_table[replacement.slot] =
                reinterpret_cast<void*>(&player_free_look_entry);
            break;
        case 0x4336ee16:
            if (context)
                context->visibility.bind_player_table(descriptor);
            else
                descriptor.replacement_table[replacement.slot] =
                    effects::EntityVisibilityRuntime::source_entry();
            break;
        default:
            throw std::logic_error("Unbound captured primary player hook");
        }
    }
}
}
void bind_player_primary_static_entries(VtableHookMetadata& metadata, PlayerPrimaryKind kind) {
    bind_primary_entries(metadata, kind, nullptr);
}
void PlayerHookFamily::bind_replacements(PlayerPrimaryKind kind) {
    // Publish actual contexts before any receiver can use its prepared table,
    // including the native installer's cleanup-node allocation failure path.
    active_runtimes = &runtimes_;
    bind_primary_entries(metadata(kind), kind, &runtimes_);
}

VtableHookNode* PlayerHookFamily::install(PlayerPrimaryKind kind, void* actual_player) {
    bind_replacements(kind);
    return install_vtable_hook(hooks_, actual_player, metadata(kind));
}

void PlayerHookFamily::clear(PlayerPrimaryKind kind) {
    clear_vtable_hook(metadata(kind), hooks_.memory);
}
} // namespace skeet::engine
