#include "skeet/client/feature_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/skins/runtime_owner.hpp"
#include "skeet/rage/runtime_owner.hpp"
#include "skeet/legit/runtime_owner.hpp"
#include "skeet/esp/runtime_owner.hpp"
#include "skeet/script/api_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
std::uint32_t receive_offset(ClientRuntimeStorage& storage, std::uint32_t identity) {
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.pointer_identity == identity)
            return static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(
                engine::DecodedOffset(storage.offsets().globals, identity, recipe.xor_key_identity,
                                      recipe.initial_xor_key, 0)));
    throw std::invalid_argument("Feature factory has no proven receive offset");
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& storage, std::uint32_t identity) {
    return {storage.owner_registries().at(identity), storage.memory()};
}
const float& frame_time(ClientRuntimeStorage& storage) {
    auto* const globals = storage.data().native_cell<const std::byte*>(0x43467B04u);
    if (!globals)
        throw std::invalid_argument("Feature factories require actual engine global variables");
    return *reinterpret_cast<const float*>(globals + 0x14);
}
void require_inputs(ClientRuntimeStorage& storage, ConfigurationRuntimeOwner& settings,
                    HistoryRuntimeOwner& history, skins::SkinsRuntimeOwner& skins,
                    rage::RuntimeOwner& rage, legit::RuntimeOwner& legit, esp::EspRuntimeOwner& esp,
                    script::ScriptApiRuntimeOwner& scripts, engine::HookRuntimeOwner& hooks) {
    if (&settings.storage() != &storage || &history.storage() != &storage ||
        &rage.storage() != &storage || &legit.storage() != &storage || &esp.storage() != &storage ||
        &scripts.storage() != &storage || &hooks.storage() != &storage ||
        &skins.sequence_bindings().settings != &settings.skins_settings() ||
        &skins.sequence_bindings().events != &storage.event_registry())
        throw std::invalid_argument("Feature factories must share the same process owners");
    auto& image = storage.data();
    if (!image.native_cell<void*>(0x4346A514u) || !image.native_cell<void*>(0x4346A890u) ||
        !image.native_cell<void*>(0x4346A4E0u))
        throw std::invalid_argument("Feature factories require acquired engine interfaces");
}
}

struct FeatureRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    misc::ZoomSensitivityBindings sensitivity;
    rage::LiveTargetRelationships event_targets;
    RuntimeOwnerFactoryBindings factories;

    Data(ClientRuntimeStorage& source, ConfigurationRuntimeOwner& settings,
         HistoryRuntimeOwner& history, skins::SkinsRuntimeOwner& skins, rage::RuntimeOwner& rage,
         legit::RuntimeOwner& legit, esp::EspRuntimeOwner& esp,
         script::ScriptApiRuntimeOwner& scripts, engine::HookRuntimeOwner& hooks)
        : storage(source), sensitivity{source.data().native_cell<void*>(0x43468994u),
                                       source.data().native_cell<void*>(0x43475808u),
                                       source.data().native_cell<std::uint32_t>(0x43467A48u),
                                       receive_offset(source, 0x43468248u)},
          event_targets{source.data().native_cell<void*>(0x4346A890u), history.game_type(),
                        history.teammates_are_enemies(), history.players().adjustments(),
                        history.target_offsets()},
          factories{source.configuration(),
                    registry(source, 0x43475B78u),
                    registry(source, 0x43475A9Cu),
                    source.entity_observers(),
                    registry(source, 0x43475A84u),
                    skins.sequence_bindings(),
                    rage.factory_bindings(),
                    source.data().native_cell<rage::LifecycleOwner*>(0x43475810u),
                    legit.factory_bindings(),
                    esp.factory_bindings(),
                    esp.active(),
                    {source.data().native_cell<void*>(0x4346A514u),
                     source.data().native_cell<void*>(0x4346A4E0u)},
                    source.data().native_cell<effects::BlurEffect*>(0x43475A20u),
                    settings.miscellaneous().console_output,
                    source.data().native_cell<misc::ColoredLogRing*>(0x434758CCu),
                    frame_time(source),
                    settings.miscellaneous().fov,
                    {sensitivity,
                     source.data().native_cell<void*>(0x4346A890u),
                     {static_cast<std::uint32_t>(history.target_offsets().rejection_bytes[0]),
                      static_cast<std::uint32_t>(history.target_offsets().movement_type),
                      receive_offset(source, 0x4346A6CCu), receive_offset(source, 0x43468F84u),
                      sensitivity.fov_offset}},
                    scripts.normal_log(),
                    event_targets,
                    hooks.console_input(),
                    source.data().native_cell<void*>(0x43467D84u)} {}
};

FeatureRuntimeOwner::FeatureRuntimeOwner(ClientRuntimeStorage& storage,
                                         ConfigurationRuntimeOwner& settings,
                                         HistoryRuntimeOwner& history,
                                         skins::SkinsRuntimeOwner& skins, rage::RuntimeOwner& rage,
                                         legit::RuntimeOwner& legit, esp::EspRuntimeOwner& esp,
                                         script::ScriptApiRuntimeOwner& scripts,
                                         engine::HookRuntimeOwner& hooks) {
    require_inputs(storage, settings, history, skins, rage, legit, esp, scripts, hooks);
    data_ =
        std::make_unique<Data>(storage, settings, history, skins, rage, legit, esp, scripts, hooks);
}
FeatureRuntimeOwner::~FeatureRuntimeOwner() = default;
ClientRuntimeStorage& FeatureRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
RuntimeOwnerFactoryBindings& FeatureRuntimeOwner::factory_bindings() noexcept {
    return data_->factories;
}
void FeatureRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    bind_runtime_owner_factories(sources, data_->factories);
}
} //namespace skeet::client
