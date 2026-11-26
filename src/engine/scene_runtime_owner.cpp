#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include <array>
#include <stdexcept>

namespace skeet::engine {
namespace {
SceneQueryBindings compose(client::ClientRuntimeStorage& storage,
                           const client::EngineRuntimeBindings& engine,
                           client::PlayerRuntimeOwner& players) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &players.storage() != &storage)
        throw std::invalid_argument("Scene queries require the canonical engine and player owners");
    const auto manifest = build_interface_manifest();
    for (const auto cell :
         std::array<std::uint32_t, 7>{0x43468304u, 0x43469268u, 0x4346A890u, 0x43467014u,
                                      0x4346A614u, 0x43469330u, 0x43467F10u}) {
        bool found = false;
        for (std::size_t index = 0; index < manifest.size(); ++index) {
            if (manifest[index].destination_cell != cell)
                continue;
            const auto current = data.native_cell<void*>(cell);
            if (!current || current != engine.interfaces.objects[index])
                throw std::invalid_argument(
                    "Scene interface does not belong to the initialized engine bundle");
            found = true;
            break;
        }
        if (!found)
            throw std::logic_error("Scene interface is absent from the build acquisition manifest");
    }
    for (const auto cell : std::array<std::uint32_t, 11>{
             0x434758B4u, 0x43475B90u, 0x43468FF0u, 0x4346A440u, 0x43467478u, 0x43468238u,
             0x43468E30u, 0x43467E88u, 0x434674E8u, 0x43468CA0u, 0x4346719Cu})
        if (!data.native_cell<const void*>(cell))
            throw std::invalid_argument(
                "Scene class, resource and trace-filter acquisition must finish first");
    const auto* globals = data.native_cell<const std::byte*>(0x43467B04u);
    const auto* interval = data.native_cell<const float*>(0x4346A338u);
    if (!globals || reinterpret_cast<std::uintptr_t>(globals) % 4 ||
        reinterpret_cast<const std::byte*>(interval) != globals + 0x20 ||
        reinterpret_cast<const std::byte*>(data.native_cell<const std::int32_t*>(0x43469300u)) !=
            globals + 0x18 ||
        !storage.offsets().words || storage.offsets().globals.pointers.empty())
        throw std::invalid_argument(
            "Scene queries require initialized live clock aliases and entity offsets");
    for (const auto cell : {0x43467A8Cu, 0x4346A568u, 0x43467A54u, 0x43467048u, 0x43467088u})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument("Scene query ConVar is unresolved");
    return {data.native_cell<void*>(0x43468304u),
            data.native_cell<void*>(0x43469268u),
            data.native_cell<void*>(0x4346A890u),
            data.native_cell<void*>(0x43467014u),
            data.native_cell<void*>(0x4346A614u),
            data.native_cell<void*>(0x43469330u),
            data.native_cell<void*>(0x43467F10u),
            data.native_cell<void*>(0x43468FF0u),
            data.native_cell<const std::int32_t*>(0x43469300u),
            data.native_cell<void* const*>(0x434758B4u),
            data.native_cell<void* const*>(0x43475B90u),
            storage.offsets().globals,
            players.adjustments(),
            data.native_cell<void*>(0x4346A440u),
            data.native_cell<void*>(0x43467478u),
            data.native_cell<const void*>(0x43468238u),
            data.native_cell<const void*>(0x43468E30u),
            data.native_cell<const void*>(0x43467E88u),
            data.native_cell<const void*>(0x434674E8u),
            data.native_cell<EngineBreakableFunction>(0x43468F30u),
            engine.variables.at(0x43467A8Cu),
            engine.variables.at(0x4346A568u),
            data.native_cell<void*>(0x43468CA0u),
            data.native_cell<void*>(0x4346719Cu),
            *interval,
            *reinterpret_cast<const float*>(globals + 0x20),
            engine.variables.at(0x43467A54u),
            engine.variables.at(0x43467048u),
            engine.variables.at(0x43467088u)};
}
}
SceneQueryRuntimeOwner::SceneQueryRuntimeOwner(client::ClientRuntimeStorage& storage,
                                               const client::EngineRuntimeBindings& engine,
                                               client::PlayerRuntimeOwner& players)
    : storage_(storage), players_(players), bindings_(compose(storage, engine, players)),
      queries_(bindings_) {}
} //namespace skeet::engine
