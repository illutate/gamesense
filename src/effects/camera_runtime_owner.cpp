#include "skeet/effects/camera_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/esp/effects_config.hpp"
#include <stdexcept>

namespace skeet::effects {
namespace {
client::ClientRuntimeStorage& require_owners(client::ClientRuntimeStorage& storage,
                                             const client::EngineRuntimeBindings& engine,
                                             client::ConfigurationRuntimeOwner& settings,
                                             client::PlayerRuntimeOwner& players) {
    if (&settings.storage() != &storage || &players.storage() != &storage ||
        &engine.variables.data() != &storage.data())
        throw std::invalid_argument("Camera contexts require the canonical source owners");
    const auto manifest = engine::build_interface_manifest();
    for (const auto cell : {0x4346A514u, 0x4346A890u}) {
        bool found = false;
        for (std::size_t index = 0; index < manifest.size(); ++index) {
            if (manifest[index].destination_cell != cell)
                continue;
            auto* current = storage.data().native_cell<void*>(cell);
            if (!current || current != engine.interfaces.objects[index])
                throw std::invalid_argument(
                    "Camera interface differs from the acquired engine bundle");
            found = true;
            break;
        }
        if (!found)
            throw std::logic_error("Camera interface is absent from the build manifest");
    }
    // CInput is acquired by initialize_build_game_state_bindings from the
    // actual client instruction operand, not a CreateInterface registration.
    if (!storage.data().native_cell<void*>(0x4346A894u))
        throw std::invalid_argument("Camera requires the acquired CInput object");
    if (!storage.offsets().words)
        throw std::invalid_argument("Camera requires initialized entity offsets");
    return storage;
}
std::uint32_t offset(const engine::NativeOffsetGlobals& globals, std::uint32_t pointer,
                     std::uint32_t key, std::uint32_t mask) {
    const auto* word = globals.pointers.at(pointer);
    if (!word)
        throw std::invalid_argument("Camera offset publication is empty");
    return *word ^ globals.keys.at(key) ^ mask;
}
CameraNetvars offsets(const engine::NativeOffsetGlobals& globals) {
    return {offset(globals, 0x43468E78u, 0x434684E4u, 0x9D920E35u),
            offset(globals, 0x4346A6CCu, 0x43468EF8u, 0x2F4E97C4u),
            offset(globals, 0x43468F84u, 0x43467E64u, 0x3B704B41u),
            offset(globals, 0x43468248u, 0x43469218u, 0xFBE8C5EAu),
            offset(globals, 0x43468D10u, 0x43468DCCu, 0x3F51AA0Au)};
}
}
CameraRuntimeOwner::CameraRuntimeOwner(client::ClientRuntimeStorage& storage,
                                       const client::EngineRuntimeBindings& engine,
                                       client::ConfigurationRuntimeOwner& settings,
                                       client::PlayerRuntimeOwner& players)
    : storage_(require_owners(storage, engine, settings, players)),
      interfaces_(storage.data().native_cell<void*>(0x4346A514u),
                  storage.data().native_cell<void*>(0x4346A890u),
                  storage.data().native_cell<void*>(0x4346A894u),
                  storage.data().native_cell<void*>(0x43475808u),
                  storage.data().native_cell<ObserverProxy>(0x4347BDE4u)),
      history_(players.cache()), runtime_(interfaces_, offsets(storage.offsets().globals), history_,
                                          settings.effects_settings().third_person_dead.value,
                                          settings.effects_settings().third_person_key.value,
                                          storage.data().native_cell<bool>(0x4347CA59u)) {
    runtime_.keys = storage.input_runtime().bitmap();
}
}
