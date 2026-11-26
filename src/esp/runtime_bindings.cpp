#include "skeet/esp/runtime_bindings.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::esp {
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::invalid_argument(message);
}
void require_interface(client::ClientRuntimeStorage& storage,
                       const client::EngineRuntimeBindings& engine, std::uint32_t cell) {
    const auto manifest = engine::build_interface_manifest();
    for (std::size_t index = 0; index < manifest.size(); ++index) {
        if (manifest[index].destination_cell != cell)
            continue;
        auto* object = storage.data().native_cell<void*>(cell);
        require(object && object == engine.interfaces.objects[index],
                "ESP requires the genuine interface from its canonical engine bundle");
        return;
    }
    throw std::logic_error("ESP interface is absent from the build acquisition manifest");
}
void require_storage(client::ClientRuntimeStorage& storage,
                     const client::EngineRuntimeBindings& engine) {
    auto& data = storage.data();
    require(&engine.variables.data() == &data && storage.offsets().words &&
                !storage.offsets().globals.pointers.empty(),
            "ESP requires initialized canonical engine variables and entity offsets");
    const auto* globals = data.native_cell<const std::byte*>(0x43467B04);
    require(globals && reinterpret_cast<std::uintptr_t>(globals) % 4 == 0 &&
                reinterpret_cast<const std::byte*>(data.native_cell<const float*>(0x4346A334)) ==
                    globals &&
                reinterpret_cast<const std::byte*>(data.native_cell<const float*>(0x43467EB8)) ==
                    globals + 0x10 &&
                reinterpret_cast<const std::byte*>(data.native_cell<const float*>(0x4346A338)) ==
                    globals + 0x20 &&
                reinterpret_cast<const std::byte*>(
                    data.native_cell<const std::int32_t*>(0x43469300)) == globals + 0x18,
            "ESP requires genuine live engine clock and maximum-player aliases");
}
}
PlayerEngineBindings native_player_engine_bindings(client::ClientRuntimeStorage& storage,
                                                   const client::EngineRuntimeBindings& engine) {
    require_storage(storage, engine);
    require_interface(storage, engine, 0x4346A890);
    require_interface(storage, engine, 0x4346A514);
    auto& data = storage.data();
    require(data.native_cell<void*>(0x43468FF0) && data.native_cell<void*>(0x4346A3A4) &&
                data.native_cell<const std::int32_t*>(0x43469060) &&
                data.native_cell<PlayerSequenceActivity>(0x4346A364),
            "ESP requires the recovered resource, game-rules, bone-counter and sequence producers");
    for (const auto cell : {0x43467A8Cu, 0x4346A694u, 0x4346A568u})
        require(engine.variables.at(cell).get() != nullptr,
                "ESP requires its acquired game-mode ConVars");
    return {data.native_cell<void*>(0x4346A890),
            data.native_cell<void*>(0x4346A514),
            data.native_cell<void*>(0x43475808),
            data.native_cell<void**>(0x43468FF0),
            data.native_cell<void**>(0x4346A3A4),
            engine.variables.at(0x43467A8C),
            engine.variables.at(0x4346A694),
            engine.variables.at(0x4346A568),
            data.native_cell<const std::int32_t*>(0x43469060),
            data.native_cell<const float*>(0x43467EB8),
            data.native_cell<const void*>(0x43467B04),
            data.native_cell<const float*>(0x4346A338),
            data.native_cell<Vector3>(0x43479CCC),
            data.native_cell<float>(0x43479CD8),
            data.native_cell<float>(0x43479CDC),
            data.native_cell<const float*>(0x4346A334),
            data.native_cell<std::uint8_t>(0x43468E58),
            data.native_cell<std::uint8_t>(0x43467224),
            data.native_cell<std::int32_t>(0x4346DB1C),
            data.native_cell<std::uint8_t>(0x4347CA30),
            data.native_cell<PlayerSequenceActivity>(0x4346A364),
            data.native_cell<const std::int32_t*>(0x43469300),
            storage.offsets()};
}
SoundOverlayBindings native_sound_overlay_bindings(client::ClientRuntimeStorage& storage,
                                                   const client::EngineRuntimeBindings& engine,
                                                   engine::SceneQueryRuntimeOwner& scene) {
    require_storage(storage, engine);
    require(&scene.storage() == &storage, "ESP sound must share the canonical scene query owner");
    // These acquisitions are mandatory for this supported build. Their native
    // call sites still preserve their nullable-interface guards on later reads.
    require_interface(storage, engine, 0x43468260);
    auto& data = storage.data();
    require(data.native_cell<void*>(0x434679EC) != nullptr,
            "ESP sound requires the acquired genuine C_TempEntsSystem inline object");
    return {scene.queries().movement(), data.native_cell<std::uint8_t*>(0x434683D0),
            data.native_cell<void*>(0x43468260), data.native_cell<void*>(0x434679EC)};
}
} // namespace skeet::esp
