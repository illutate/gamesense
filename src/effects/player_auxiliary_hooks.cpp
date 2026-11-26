#include "skeet/effects/player_auxiliary_hooks.hpp"
#include "skeet/effects/entity_visibility.hpp"
#include "skeet/effects/free_look_capture.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include <stdexcept>

namespace skeet::effects {
namespace {
std::uint32_t offset(client::ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& globals = storage.offsets().globals;
    // Resolve the exact entity publications before salt lookup; the origin
    // salt is also used by an unrelated temporary bullet event.
    struct Derived {
        std::uint32_t pointer, key, salt;
    };
    constexpr std::array derived{Derived{0x43468D30, 0x43468ED4, 0x2A671B42},
                                 Derived{0x43468368, 0x43467FAC, 0x3B1F6ECC},
                                 Derived{0x43468BF8, 0x43467B8C, 0xD070684C}};
    for (const auto& recipe : derived)
        if (recipe.salt == salt)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(globals, recipe.pointer, recipe.key, salt, 0)));
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                      recipe.initial_xor_key, 0)));
    for (const auto& recipe : engine::build_data_map_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                      recipe.initial_xor_key, 0)));
    throw std::invalid_argument("Player hook field has no proven receive/datamap recipe");
}
EntityNetworkableOffsets ragdoll_fields(client::ClientRuntimeStorage& storage) {
    return {{offset(storage, 0x73DBBB49), offset(storage, 0x38CA5228), offset(storage, 0x9C14F5A7),
             offset(storage, 0x1B5F47D5), offset(storage, 0xFC2723F7), offset(storage, 0x8481EA04),
             offset(storage, 0x258BF82B), offset(storage, 0xA9EE6062), offset(storage, 0x2A671B42),
             offset(storage, 0x3B1F6ECC), offset(storage, 0xC865F10E)},
            offset(storage, 0x688E0292),
            offset(storage, 0xD070684C),
            offset(storage, 0x2F4E97C4),
            offset(storage, 0x3B704B41)};
}
client::ClientRuntimeStorage& checked_storage(client::ClientRuntimeStorage& storage,
                                              const client::EngineRuntimeBindings& engine,
                                              client::ConfigurationRuntimeOwner& configuration,
                                              client::HistoryRuntimeOwner& history,
                                              animation::RuntimeOwner& animation,
                                              engine::HookRuntimeOwner& hooks) {
    auto& data = storage.data();
    const auto core = engine.interfaces.core();
    const auto& entity = history.entity_runtime();
    auto* globals = data.native_cell<std::byte*>(0x43467B04);
    if (&configuration.storage() != &storage || &history.storage() != &storage ||
        &history.players().storage() != &storage || &animation.storage() != &storage ||
        &hooks.storage() != &storage || &animation.players() != &history.players().cache() ||
        &engine.variables.data() != &data ||
        core.engine_client != data.native_cell<void*>(0x4346A514) ||
        core.entity_list != data.native_cell<void*>(0x4346A890) ||
        core.engine_trace != data.native_cell<void*>(0x43468304) ||
        entity.engine != core.engine_client || entity.entity_list != core.entity_list ||
        history.game_type().get() != engine.variables.at(0x43467A8C).get() ||
        history.teammates_are_enemies().get() != engine.variables.at(0x4346A568).get())
        throw std::invalid_argument(
            "Player auxiliary hooks must share canonical engine, history, animation and configuration owners");
    for (const auto cell :
         {0x4346A514u, 0x4346A890u, 0x43468304u, 0x4346A894u, 0x43467014u, 0x434758B4u})
        if (!data.native_cell<void*>(cell))
            throw std::invalid_argument(
                "Player auxiliary hooks require the actual input, trace and engine interfaces");
    if (!globals || !data.native_cell<std::int32_t*>(0x434678F8) ||
        reinterpret_cast<std::byte*>(data.native_cell<float*>(0x4346A338)) != globals + 0x20 ||
        &entity.interval != data.native_cell<float*>(0x4346A338) ||
        !data.native_cell<InvalidateEntityPhysics>(0x43468C10) ||
        !data.native_cell<engine::MemberFunction<void>>(0x43468320))
        throw std::invalid_argument(
            "Player auxiliary hooks require the published engine clock and render/physics entries");
    for (const auto identity : {0x43467A8Cu, 0x4346A568u, 0x4346A67Cu, 0x43467A78u})
        if (!engine.variables.at(identity).get())
            throw std::invalid_argument(
                "Player auxiliary hooks require their genuine acquired ConVars");
    return storage;
}
}
struct PlayerAuxiliaryHooks::Data {
    client::ClientRuntimeStorage& storage;
    client::HistoryRuntimeOwner& history;
    EntityVisibilityRelationships relationships;
    EntityVisibilityBindings visibility_bindings;
    EntityVisibilityRuntime visibility;
    FreeLookCaptureBindings capture;
    EntityNetworkableBindings ragdoll_bindings;
    EntityNetworkableRuntime ragdolls;
    Data(client::ClientRuntimeStorage& source, const client::EngineRuntimeBindings& engine,
         client::ConfigurationRuntimeOwner& configuration,
         client::HistoryRuntimeOwner& history_owner, animation::RuntimeOwner& animation,
         engine::HookRuntimeOwner& hooks)
        : storage(checked_storage(source, engine, configuration, history_owner, animation, hooks)),
          history(history_owner),
          relationships{source.data().native_cell<void*>(0x4346A890), history.game_type(),
                        history.teammates_are_enemies(), history.players().adjustments(),
                        history.target_offsets()},
          visibility_bindings{source.data().native_cell<void*>(0x4346A514),
                              &source.data().native_cell<void*>(0x43475808), relationships},
          visibility{configuration.effects_settings(), visibility_bindings},
          capture{source.data().native_cell<void*>(0x4346A894),
                  source.data().native_cell<void*>(0x43468304),
                  source.data().native_cell<void*>(0x43467014),
                  source.data().native_cell<void*>(0x4346A890),
                  source.data().native_cell<void* const*>(0x434758B4),
                  offset(source, 0x4E36DFD7),
                  source.data().native_cell<FreeLookOwner*>(0x43475A64),
                  source.data().native_cell<std::int32_t*>(0x434678F8),
                  animation.trigonometry()},
          ragdoll_bindings{visibility_bindings,
                           source.data().native_cell<void*>(0x4346A890),
                           source.data().native_cell<std::uint32_t>(0x4346A67C),
                           history.entity_runtime().offsets,
                           ragdoll_fields(source),
                           source.data().native_cell<InvalidateEntityPhysics>(0x43468C10),
                           source.data().native_cell<engine::MemberFunction<void>>(0x43468320),
                           source.data().native_cell<void*>(0x43467B04),
                           source.data().native_cell<std::uint32_t>(0x43467A78),
                           hooks.registry()},
          ragdolls{configuration.effects_settings(), configuration.colored_model_settings(),
                   history.players().cache(), ragdoll_bindings} {}
};
PlayerAuxiliaryHooks::PlayerAuxiliaryHooks(client::ClientRuntimeStorage& storage,
                                           const client::EngineRuntimeBindings& engine,
                                           client::ConfigurationRuntimeOwner& configuration,
                                           client::HistoryRuntimeOwner& history,
                                           animation::RuntimeOwner& animation,
                                           engine::HookRuntimeOwner& hooks)
    : data_(std::make_unique<Data>(storage, engine, configuration, history, animation, hooks)) {}
PlayerAuxiliaryHooks::~PlayerAuxiliaryHooks() = default;
client::ClientRuntimeStorage& PlayerAuxiliaryHooks::storage() const noexcept {
    return data_->storage;
}
client::HistoryRuntimeOwner& PlayerAuxiliaryHooks::history() const noexcept {
    return data_->history;
}
EntityVisibilityRuntime& PlayerAuxiliaryHooks::visibility() noexcept {
    return data_->visibility;
}
EntityNetworkableRuntime& PlayerAuxiliaryHooks::ragdolls() noexcept {
    return data_->ragdolls;
}
const FreeLookCaptureBindings& PlayerAuxiliaryHooks::free_look_capture() const noexcept {
    return data_->capture;
}
}
