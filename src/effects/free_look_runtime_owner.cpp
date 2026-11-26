#include "skeet/effects/free_look_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/esp/effects_config.hpp"
#include "skeet/misc/config.hpp"
#include <stdexcept>

namespace skeet::effects {
namespace {
client::ClientRuntimeStorage& checked(client::ClientRuntimeStorage& storage,
                                      const client::EngineRuntimeBindings& engine,
                                      client::ConfigurationRuntimeOwner& config,
                                      client::CrtRuntimeOwner& crt) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &crt.storage() != &storage)
        throw std::invalid_argument("Free-look must share the canonical process owners");
    if (!data.native_cell<void*>(0x4346A894u) || !data.native_cell<void*>(0x43469264u) ||
        !data.native_cell<void*>(0x43468304u) || !data.native_cell<void* const*>(0x434758B4u) ||
        !data.native_cell<std::int32_t*>(0x434678F8u) || !engine.variables.at(0x43467AA0u).get() ||
        !engine.variables.at(0x4346A7DCu).get())
        throw std::invalid_argument(
            "Free-look requires its acquired camera, trace and clock inputs");
    return storage;
}
engine::OwnerRegistryBinding registry(client::ClientRuntimeStorage& storage,
                                      std::uint32_t address) {
    return {storage.owner_registries().at(address), storage.memory()};
}
}
FreeLookRuntimeOwner::FreeLookRuntimeOwner(client::ClientRuntimeStorage& storage,
                                           const client::EngineRuntimeBindings& engine,
                                           client::ConfigurationRuntimeOwner& config,
                                           client::CrtRuntimeOwner& crt)
    : storage_(checked(storage, engine, config, crt)),
      view_{storage.data().native_cell<void*>(0x4346A894u),
            storage.data().native_cell<void*>(0x43469264u),
            storage.data().native_cell<void*>(0x43475808u),
            static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(engine::DecodedOffset(
                storage.offsets().globals, 0x43468E78u, 0x434684E4u, 0x9D920E35u, 0))),
            *storage.data().native_cell<std::int32_t*>(0x434678F8u),
            engine.variables.at(0x43467AA0u),
            engine.variables.at(0x4346A7DCu),
            storage.data().native_cell<void*>(0x43468304u),
            storage.data().native_cell<void* const*>(0x434758B4u),
            config.free_look().key.value.binding,
            config.effects_settings().third_person_alive.value,
            config.effects_settings().third_person_key.value.binding,
            storage.input_runtime().bitmap(),
            crt.trigonometry()},
      initialization_{registry(storage, 0x43475B78u),
                      registry(storage, 0x43475988u),
                      {state_, view_, storage.data().native_cell<FreeLookOwner*>(0x43475A64u)}} {}
void FreeLookRuntimeOwner::bind_initializers(client::NativeStartupSources& sources) {
    bind_free_look_initializer(sources, initialization_);
}
}
