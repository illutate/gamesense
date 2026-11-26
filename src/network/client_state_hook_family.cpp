#include "skeet/network/client_state_hook_family.hpp"
#include "skeet/network/client_state_hooks.hpp"
#include "skeet/network/tick_update_dispatch.hpp"
#include "skeet/network/incoming_update.hpp"
#include "skeet/network/shared_esp.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/effects/world.hpp"
#include "skeet/events/event_hook.hpp"
#include <array>

namespace skeet::network {
std::span<const ClientStateSourceReplacement, 13> client_state_source_replacements() {
    static const std::array<ClientStateSourceReplacement, 13> entries{
        {{client_state_native_replacements[0], reinterpret_cast<void*>(&client_state_slot2_hook)},
         {client_state_native_replacements[1], reinterpret_cast<void*>(&incoming_tick_hook)},
         {client_state_native_replacements[2], reinterpret_cast<void*>(&incoming_update_hook)},
         {client_state_native_replacements[3], reinterpret_cast<void*>(&client_state_slot12_hook)},
         {client_state_native_replacements[4],
          reinterpret_cast<void*>(&effects::flash_effect_hook)},
         {client_state_native_replacements[5], reinterpret_cast<void*>(&client_state_slot15_hook)},
         {client_state_native_replacements[6],
          reinterpret_cast<void*>(&process_voice_message_hook)},
         {client_state_native_replacements[7], reinterpret_cast<void*>(&client_state_slot25_hook)},
         {client_state_native_replacements[8], reinterpret_cast<void*>(&client_state_slot32_hook)},
         {client_state_native_replacements[9], reinterpret_cast<void*>(&events::event_packet_hook)},
         {client_state_native_replacements[10], reinterpret_cast<void*>(&update_status_hook)},
         {client_state_native_replacements[11], reinterpret_cast<void*>(&client_state_slot47_hook)},
         {client_state_native_replacements[12],
          reinterpret_cast<void*>(&client::entity_update_hook)}}};
    return entries;
}

ClientStateHookFamily::ClientStateHookFamily(engine::HookDescriptorStorage& storage,
                                             engine::VtableHookRegistry& hooks,
                                             engine::NativeImageEnvironment images,
                                             engine::HookDescriptorSystem system,
                                             ClientStateHookRuntimes runtimes)
    : metadata_(storage.at(client_state_descriptor_identity)), hooks_(hooks),
      engine_module_(storage.image().native_cell<std::byte*>(client_state_module_identity)),
      images_(images), system_(system), runtimes_(runtimes) {}

void ClientStateHookFamily::prepare() {
    engine::prepare_rtti_vtable_hook(metadata_, engine_module_, client_state_rtti_hash,
                                     client_state_rtti_base_hash, images_, hooks_.memory, system_);
    bind_replacements();
}

void ClientStateHookFamily::bind_replacements() {
    // Publish the actual source owners before4331E834 can install their table,
    // including its observed cleanup-node allocation failure path.
    bind_client_state_hooks_runtime(runtimes_.core);
    bind_tick_update_runtime(runtimes_.ticks);
    bind_incoming_update_runtime(runtimes_.incoming);
    effects::bind_flash_effect_runtime(runtimes_.effects);
    bind_shared_esp_runtime(runtimes_.shared_esp);
    events::bind_event_packet_hook_runtime(runtimes_.events);
    client::bind_entity_update_runtime(runtimes_.entities);
    for (const auto& replacement : client_state_source_replacements())
        metadata_.replacement_table[replacement.native.slot] = replacement.source_entry;
}

engine::VtableHookNode* ClientStateHookFamily::install(void* actual_receiver) {
    return engine::install_vtable_hook(hooks_, actual_receiver, metadata_);
}
}
