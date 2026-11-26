#include "skeet/misc/viewmodel_runtime_owner.hpp"
#include "skeet/misc/viewmodel_lag_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/effects/runtime_owner.hpp"
#include "skeet/skins/runtime_owner.hpp"
#include "skeet/skins/config.hpp"
#include "skeet/menu/settings_menu_storage.hpp"
#include <new>
#include <stdexcept>

namespace skeet::misc {
namespace {
std::ptrdiff_t field(client::ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& globals = storage.offsets().globals;
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                         salt, 0);
    for (const auto& recipe : engine::build_data_map_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                         salt, 0);
    if (salt == 0x3EDECE61u)
        return engine::DecodedOffset(globals, 0x434675C8u, 0x4346A5B8u, salt, 0);
    throw std::invalid_argument("Viewmodel field lacks its recovered offset recipe");
}
ViewmodelLagOffsets offsets(client::ClientRuntimeStorage& s) {
    const auto f = [&](std::uint32_t salt) { return field(s, salt); };
    return {f(0x71DCEE42), f(0x38CA5228), f(0xD5FD2E2C), f(0xA116369A),
            f(0xA25D8271), f(0x695FF325), f(0xAEF72A04), f(0x43D8CA19),
            f(0x53E7C7D2), f(0x47877DEB), f(0xBEFF3172)};
}
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t cell) {
    auto* pointer = data.native_cell<T*>(cell);
    if (!pointer)
        throw std::invalid_argument(
            "Viewmodel contexts require their acquired global clock aliases");
    return *pointer;
}
engine::OwnerRegistryBinding registry(client::ClientRuntimeStorage& s, std::uint32_t cell) {
    return {s.owner_registries().at(cell), s.memory()};
}
client::ClientRuntimeStorage&
checked(client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
        client::ConfigurationRuntimeOwner& config, skins::SkinsRuntimeOwner& skins,
        effects::EffectsRuntimeOwner& effects, engine::HookRuntimeOwner& hooks) {
    auto& data = storage.data();
    const auto& sequence = skins.sequence_bindings();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &effects.storage() != &storage || &hooks.storage() != &storage ||
        &hooks.descriptor_storage().image() != &data ||
        &sequence.settings != &config.skins_settings() ||
        &sequence.state != &skins.sequence_state() ||
        &sequence.entity_list != &data.native_cell<void*>(0x4346A890u) ||
        &sequence.events != &storage.event_registry())
        throw std::invalid_argument(
            "Viewmodel contexts require canonical settings, skins sequence history, effects and hooks");
    if (!storage.offsets().words || storage.offsets().globals.pointers.empty() ||
        !data.native_cell<void*>(0x4346A894u) || !data.native_cell<void*>(0x43467B04u) ||
        !engine.variables.at(0x4346A850u).get())
        throw std::invalid_argument(
            "Viewmodel contexts require acquired input, clock, offsets and interpolation ConVar");
    for (const auto cell : {0x4346A514u, 0x4346A890u}) {
        bool found = false;
        const auto manifest = engine::build_interface_manifest();
        for (std::size_t i = 0; i < manifest.size(); ++i)
            if (manifest[i].destination_cell == cell) {
                if (!data.native_cell<void*>(cell) ||
                    data.native_cell<void*>(cell) != engine.interfaces.objects[i])
                    throw std::invalid_argument(
                        "Viewmodel interface differs from its acquired canonical object");
                found = true;
                break;
            }
        if (!found)
            throw std::logic_error("Viewmodel interface is absent from the acquisition manifest");
    }
    return storage;
}
struct StateStorage {
    engine::JobMemory memory;
    ViewmodelLagState* value;
    StateStorage(client::ClientRuntimeStorage& storage,
                 std::array<skins::SequenceHistory, 150>& history)
        : memory(storage.memory()),
          value(static_cast<ViewmodelLagState*>(
              engine::allocate_owner_storage(memory, sizeof(ViewmodelLagState)))) {
        if (!value)
            throw std::bad_alloc();
        // The external canonical state retains the native allocation's ZERO
        // policy; its one history reference names the existing skins ring.
        ::new (static_cast<void*>(value)) ViewmodelLagState(history);
    }
    ~StateStorage() {
        value->~ViewmodelLagState();
        engine::release_owner_storage(memory, value);
    }
};
}
struct ViewmodelRuntimeOwner::Data {
    client::ClientRuntimeStorage& storage;
    StateStorage state;
    ViewmodelHookInputs inputs;
    ViewmodelHooksRuntime hooks;
    ViewmodelLagOwnerRegistries registries;
    ViewmodelLagFactoryContext factory;
    Data(client::ClientRuntimeStorage& source, const client::EngineRuntimeBindings& engine,
         client::ConfigurationRuntimeOwner& config, skins::SkinsRuntimeOwner& skins,
         effects::EffectsRuntimeOwner& effects, engine::HookRuntimeOwner& hook_owner)
        : storage(checked(source, engine, config, skins, effects, hook_owner)),
          state(storage, skins.sequence_state().history),
          inputs{source.data().native_cell<void*>(0x4346A514u),
                 source.data().native_cell<void*>(0x4346A890u),
                 source.data().native_cell<void*>(0x4346A894u),
                 source.data().native_cell<void*>(0x43475808u),
                 engine.variables.at(0x4346A850u),
                 pointed<float>(source.data(), 0x43467EB8u),
                 pointed<float>(source.data(), 0x4346A338u),
                 reinterpret_cast<float*>(source.data().native_cell<void*>(0x43467B04u))[9],
                 offsets(source),
                 field(source, 0x3EDECE61u),
                 source.data().native_cell<std::uint32_t>(0x4346A534u)},
          hooks(*state.value,
                source.data().native_cell<ViewmodelLagOwner*>(viewmodel_lag_owner_slot),
                config.settings_menu().values(), inputs, effects.view(), hook_owner.registry(),
                hook_owner.descriptor_storage()),
          registries{registry(source, 0x43475B78u), source.prediction_observers(),
                     source.entity_observers(),     source.command_phases(),
                     source.recharge_observers(),   registry(source, 0x43475AD8u)},
          factory{registries, hooks} {}
};
ViewmodelRuntimeOwner::ViewmodelRuntimeOwner(client::ClientRuntimeStorage& s,
                                             const client::EngineRuntimeBindings& e,
                                             client::ConfigurationRuntimeOwner& c,
                                             skins::SkinsRuntimeOwner& skins,
                                             effects::EffectsRuntimeOwner& effects,
                                             engine::HookRuntimeOwner& hooks)
    : data_(std::make_unique<Data>(s, e, c, skins, effects, hooks)) {}
ViewmodelRuntimeOwner::~ViewmodelRuntimeOwner() = default;
client::ClientRuntimeStorage& ViewmodelRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
ViewmodelLagState& ViewmodelRuntimeOwner::state() noexcept {
    return *data_->state.value;
}
ViewmodelHooksRuntime& ViewmodelRuntimeOwner::hooks() noexcept {
    return data_->hooks;
}
ViewmodelLagFactoryContext& ViewmodelRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void ViewmodelRuntimeOwner::bind_initializers(client::NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("Viewmodel factory requires its canonical source image");
    bind_viewmodel_lag_factory(sources, data_->factory);
}
} //namespace skeet::misc
