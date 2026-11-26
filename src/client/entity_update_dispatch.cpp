#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/menu/skins_menu.hpp"
#include <cstring>

namespace skeet::client {
namespace {
EntityUpdateRuntime* runtime = nullptr;
void apply_patches(EntityUpdatePatch* head, bool before,
                   EntityUpdateBindings::ExchangeWord exchange) {
    for (auto* patch = head; patch; patch = patch->next) {
        if (!patch->target)
            continue;
        const auto replacement = before ? patch->before : patch->after;
        if (replacement)
            exchange(patch->target, replacement);
    }
}
}
void* register_entity_update_receiver(engine::OwnerRegistry& registry, void* object,
                                      engine::JobMemory memory) {
    //4333BF32 shares433F1665 and4334DC99 with the owner pointer registry.
    return engine::register_owner(registry, object, memory);
}
void dispatch_entity_update(void* object, std::uintptr_t incoming, EntityUpdateGlobals& globals,
                            const EntityUpdateBindings& bindings, ui::SkinsMenu& skins_menu) {
    globals.phase = 1;
    apply_patches(globals.patches, true, bindings.exchange);
    //433205F9..4332061C rereads the installed metadata after the first patch
    // pass. A patch can change the receiver/table, so this is not cached.
    const auto* table = *static_cast<void***>(object);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    reinterpret_cast<engine::MemberFunction<void, std::uintptr_t>>(metadata->original_table[64])(
        object, incoming);
    apply_patches(globals.patches, false, bindings.exchange);

    const auto index = engine::invoke<std::int32_t>(bindings.engine_client, 0x30 / 4);
    auto* local = engine::invoke<void*, std::int32_t>(bindings.entity_list, 0xC / 4, index);
    if (local && !engine::invoke<bool>(bindings.engine_client, 0x148 / 4) &&
        !engine::invoke<bool>(bindings.engine_client, 0x174 / 4)) {
        std::int32_t team;
        std::memcpy(&team, static_cast<const std::byte*>(local) + bindings.team_offset,
                    sizeof(team));
        if (team != globals.local_team) {
            globals.local_team = team;
            skins_menu.select_team(team); //complete existing4334A06E body
        }
    }
    // The concrete registry captures its range after the team-menu update and
    // directly invokes the recovered bodies for the registered native tables.
    globals.receivers.update_entities(local, incoming);
    globals.phase = 4;
}
void bind_entity_update_runtime(EntityUpdateRuntime& value) noexcept {
    runtime = &value;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall entity_update_hook(void* object, void*, std::uintptr_t incoming) {
#else
void entity_update_hook(void* object, void*, std::uintptr_t incoming) {
#endif
    runtime->process(object, incoming);
}
} //namespace skeet::client
