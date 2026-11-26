#include "skeet/client/target_mask_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/rage/settings.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& config) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        engine.interfaces.core().engine_client != data.native_cell<void*>(0x4346A514u) ||
        !data.native_cell<void*>(0x4346A514u) || !data.native_cell<std::byte*>(0x43467B04u) ||
        !engine.variables.at(0x43467A78u).get())
        throw std::invalid_argument(
            "Target-mask requires the actual configuration, engine and latency state");
    return storage;
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage, std::uint32_t address) {
    return {storage.owner_registries().at(address), storage.memory()};
}
}
TargetMaskRuntimeOwner::TargetMaskRuntimeOwner(ClientRuntimeStorage& storage,
                                               const EngineRuntimeBindings& engine,
                                               ConfigurationRuntimeOwner& config)
    : storage_(checked(storage, engine, config)),
      command_(config.target_mask(),
               {storage.data().native_cell<void*>(0x4346A514u),
                reinterpret_cast<const float*>(
                    storage.data().native_cell<const std::byte*>(0x43467B04u) + 0x20),
                &config.rage_settings().global.enabled.value, engine.variables.at(0x43467A78u),
                &storage.data().native_cell<double>(0x4347A500u),
                &storage.data().native_cell<double>(0x4347B118u)}),
      factories_{{registry(storage, 0x43475B78u), storage.command_phases(),
                  registry(storage, 0x43475B94u), storage.entity_observers(),
                  registry(storage, 0x43475A9Cu)},
                 command_,
                 config.target_mask(),
                 storage.data_owner()} {}
void TargetMaskRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    bind_native_target_mask_factory(sources, factories_);
}
}
