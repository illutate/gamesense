#include "skeet/players/menu_bindings.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include <stdexcept>

namespace skeet::players {
namespace {
engine::DecodedOffset field(client::ClientRuntimeStorage& storage, std::uint32_t identity) {
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.pointer_identity == identity)
            return {storage.offsets().globals, identity, recipe.xor_key_identity,
                    recipe.initial_xor_key, 0};
    throw std::invalid_argument("Players menu has no recovered receive-offset recipe");
}
}
MenuBindings::MenuBindings(client::ClientRuntimeStorage& storage,
                           const client::EngineRuntimeBindings& engine)
    : live_{storage.data().native_cell<void*>(0x4346A514u),
            storage.data().native_cell<void*>(0x4346A890u),
            storage.data().native_cell<void*>(0x43475808u),
            storage.data().native_cell<void**>(0x43468FF0u),
            engine.variables.at(0x4346A568u),
            storage.data().native_cell<const std::int32_t*>(0x43469300u),
            storage.data().native_cell<std::uint8_t>(0x43467F98u),
            storage.data().native_cell<std::uint8_t>(0x43468598u),
            field(storage, 0x43467CB8u),
            field(storage, 0x434684F0u),
            field(storage, 0x434688B0u)} {
    if (&engine.variables.data() != &storage.data() || !live_.engine || !live_.entity_list ||
        !live_.player_resource || !live_.maximum_clients || !live_.teammates_are_enemies.get())
        throw std::invalid_argument(
            "Players menu requires its genuine engine and live data producers");
    bindings_.live = &live_;
}
} //namespace skeet::players
