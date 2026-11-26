#include "skeet/hud/simple_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/esp/other_config.hpp"
#include <stdexcept>

namespace skeet::hud {
namespace {
client::ClientRuntimeStorage& checked(client::ClientRuntimeStorage& storage,
                                      client::ConfigurationRuntimeOwner& configuration,
                                      client::VisualRuntimeOwner& visual,
                                      engine::HookRuntimeOwner& hooks,
                                      script::SourceScriptRuntimeOwner& scripts) {
    if (&configuration.storage() != &storage || &visual.storage() != &storage ||
        &hooks.storage() != &storage || &scripts.storage() != &storage ||
        !storage.data().native_cell<void*>(0x4346A890u) ||
        !storage.data().native_cell<void*>(0x4346A87Cu))
        throw std::invalid_argument(
            "Simple HUD must share the real renderer, channel, Lua and engine owners");
    return storage;
}
engine::OwnerRegistryBinding registry(client::ClientRuntimeStorage& storage,
                                      std::uint32_t address) {
    return {storage.owner_registries().at(address), storage.memory()};
}
void ping(void*) {
    initialize_ping_hud_owner();
}
void crosshair(void*) {
    initialize_crosshair_hud_owner();
}
}
SimpleHudRuntimeOwner::SimpleHudRuntimeOwner(client::ClientRuntimeStorage& storage,
                                             client::ConfigurationRuntimeOwner& configuration,
                                             client::VisualRuntimeOwner& visual,
                                             engine::HookRuntimeOwner& hooks,
                                             script::SourceScriptRuntimeOwner& scripts)
    : storage_(checked(storage, configuration, visual, hooks, scripts)),
      context_{registry(storage, 0x43475B78u),
               registry(storage, 0x43475A9Cu),
               scripts.bridge(),
               configuration.other_esp_settings().feature_indicators,
               hooks.channel(),
               visual.indicators(),
               configuration.other_esp_settings().overlays,
               storage.data().native_cell<void*>(0x4346A890u),
               storage.data().native_cell<void*>(0x4346A87Cu),
               storage.offsets(),
               storage.data().native_cell<std::uint8_t>(0x43468E58u)} {}
void SimpleHudRuntimeOwner::bind_initializers(client::NativeStartupSources& sources) {
    if (&sources.data() != &storage_.data())
        throw std::invalid_argument("HUD startup uses another process storage");
    bind_simple_hud_owners_context(context_);
    sources.bind_initializer({0x4347DD87u, &context_, ping});
    sources.bind_initializer({0x43486BF7u, &context_, crosshair});
}
}
