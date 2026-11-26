#include "skeet/effects/player_opacity_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/misc/config.hpp"
#include <stdexcept>

namespace skeet::effects {
namespace {
client::ClientRuntimeStorage& checked(client::ClientRuntimeStorage& storage,
                                      const client::EngineRuntimeBindings& engine,
                                      client::ConfigurationRuntimeOwner& settings,
                                      client::PlayerRuntimeOwner& players) {
    auto& data = storage.data();
    const auto core = engine.interfaces.core();
    if (&engine.variables.data() != &data || &settings.storage() != &storage ||
        &players.storage() != &storage || !core.engine_client || !core.entity_list ||
        core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        !data.native_cell<void*>(0x4346A894u) ||
        players.adjustments().settings_base() != &data.native_cell<std::uint8_t>(0x4347BE08u))
        throw std::invalid_argument(
            "Player opacity requires its canonical engine, configuration and player owners");
    const auto* globals = data.native_cell<std::byte*>(0x43467B04u);
    if (!globals || data.native_cell<void*>(0x4346A338u) != globals + 0x20 ||
        data.native_cell<void*>(0x43468D4Cu) != globals + 0x1C ||
        !data.native_cell<void*>(0x434679C0u))
        throw std::invalid_argument(
            "Player opacity requires the published smoke registry and global clock aliases");
    for (const auto address : {0x43467A8Cu, 0x4346A568u, 0x43467A78u})
        if (!engine.variables.at(address).get())
            throw std::invalid_argument("Player opacity requires discovered native ConVars");
    return storage;
}
std::uint32_t offset(const engine::NativeOffsetGlobals& globals, std::uint32_t pointer,
                     std::uint32_t key, std::uint32_t salt) {
    return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(globals, pointer, key, salt, 0)));
}
ModelRelationshipOffsets relationships(const engine::NativeOffsetGlobals& globals) {
    return {offset(globals, 0x43468E78u, 0x434684E4u, 0x9D920E35u),
            offset(globals, 0x43468BD4u, 0x4346A678u, 0x7501CB91u),
            offset(globals, 0x4346A6CCu, 0x43468EF8u, 0x2F4E97C4u),
            offset(globals, 0x43468F84u, 0x43467E64u, 0x3B704B41u),
            offset(globals, 0x434688B0u, 0x4346A518u, 0x8B5586F6u),
            offset(globals, 0x434678ACu, 0x43467B7Cu, 0x430C8993u)};
}
PlayerOpacityOffsets opacity_offsets(const engine::NativeOffsetGlobals& globals) {
    return {offset(globals, 0x43467DD0u, 0x4346A610u, 0xAF649DACu),
            offset(globals, 0x43468BF8u, 0x43467B8Cu, 0xD070684Cu),
            offset(globals, 0x43468A34u, 0x43468C00u, 0xC246315Eu),
            offset(globals, 0x434679F8u, 0x434675F8u, 0x3309C0CFu),
            offset(globals, 0x4346A530u, 0x43467564u, 0x58AE2423u),
            offset(globals, 0x43467AC0u, 0x43468E60u, 0xF267E956u)};
}
}
PlayerOpacityOwner::PlayerOpacityOwner(client::ClientRuntimeStorage& storage,
                                       const client::EngineRuntimeBindings& engine,
                                       client::ConfigurationRuntimeOwner& settings,
                                       client::PlayerRuntimeOwner& players)
    : storage_(checked(storage, engine, settings, players)),
      entities_{engine.interfaces.core().engine_client,
                engine.interfaces.core().entity_list,
                storage_.data().native_cell<void*>(0x43475808u),
                relationships(storage_.offsets().globals),
                engine.variables.at(0x43467A8Cu),
                engine.variables.at(0x4346A568u)},
      bindings_{storage_.data().native_cell<EffectsCommandOwner*>(0x43475A1Cu),
                settings.colored_model_settings(),
                entities_,
                players.adjustments(),
                storage_.data().native_cell<void*>(0x4346A894u),
                storage_.data().native_cell<void*>(0x434679C0u),
                storage_.data().native_cell<const std::int32_t*>(0x43468D4Cu),
                *storage_.data().native_cell<float*>(0x4346A338u),
                storage_.data().native_cell<RageVector>(0x43479CCCu),
                settings.hide_from_obs().enabled.value,
                engine.variables.at(0x43467A78u),
                opacity_offsets(storage_.offsets().globals)} {}
} //namespace skeet::effects
