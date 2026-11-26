#include "skeet/rage/prediction_mask_runtime_owner.hpp"
#include "skeet/rage/target_relationships.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/esp/runtime_owner.hpp"
#include "skeet/anti_aim/targets.hpp"
#include <stdexcept>

namespace skeet::rage {
namespace {
using client::ClientRuntimeStorage;
std::uint32_t field(ClientRuntimeStorage& s, std::uint32_t hash) {
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.initial_xor_key == hash)
            return std::uint32_t(std::ptrdiff_t(
                engine::DecodedOffset(s.offsets().globals, row.pointer_identity,
                                      row.xor_key_identity, row.initial_xor_key, 0)));
    throw std::invalid_argument("Prediction-mask field has no recovered receive recipe");
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
                              client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
                              engine::SceneQueryRuntimeOwner& q, esp::EspRuntimeOwner& esp) {
    if (&e.variables.data() != &s.data() || &c.storage() != &s || &h.storage() != &s ||
        &q.storage() != &s || &q.players() != &h.players() || &esp.storage() != &s ||
        &esp.active() != &s.data().native_cell<esp::EspOwnerRuntime*>(0x434758DCu))
        throw std::invalid_argument(
            "Prediction masks require canonical engine, configuration, history, scene and late ESP owners");
    if (!s.data().native_cell<void*>(0x4346A514u) || !s.data().native_cell<void*>(0x4346A890u) ||
        !s.data().native_cell<void*>(0x43469300u) || !s.data().native_cell<void*>(0x43475B90u))
        throw std::invalid_argument(
            "Prediction masks require actual engine, entity-limit and filter publications");
    return s;
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& s, std::uint32_t identity) {
    return {s.owner_registries().at(identity), s.memory()};
}
}
struct PredictionMaskRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    LiveTargetRelationships targeting;
    anti_aim::RawTargetTraceEntities entities;
    LivePredictionMaskBindings live;
    PredictionMaskFactoryBindings factory;
    Data(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
         client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
         engine::SceneQueryRuntimeOwner& q, esp::EspRuntimeOwner& esp)
        : storage(checked(s, e, c, h, q, esp)),
          targeting{s.data().native_cell<void*>(0x4346A890u), h.game_type(),
                    h.teammates_are_enemies(), h.players().adjustments(), h.target_offsets()},
          entities(s.data().native_cell<void*>(0x4346A890u), field(s, 0xE4E2D1A7u),
                   field(s, 0x49D93E62u) - 0x18u),
          live{s.data().native_cell<void*>(0x4346A514u),
               s.data().native_cell<void*>(0x43475808u),
               targeting,
               c.rage_settings().global.quick_stop.value,
               reinterpret_cast<const std::uint32_t&>(
                   c.rage_settings().global.quick_stop_options.value),
               s.data().native_cell<std::int32_t*>(0x43469300u),
               esp.active(),
               q.queries().ballistics(),
               s.data().native_cell<void**>(0x43475B90u),
               entities},
          factory{registry(s, 0x43475B78u), registry(s, 0x43475AD8u), live,
                  s.data().native_cell<PredictionMaskOwner*>(0x43475A58u)} {}
};
PredictionMaskRuntimeOwner::PredictionMaskRuntimeOwner(ClientRuntimeStorage& s,
                                                       const client::EngineRuntimeBindings& e,
                                                       client::ConfigurationRuntimeOwner& c,
                                                       client::HistoryRuntimeOwner& h,
                                                       engine::SceneQueryRuntimeOwner& q,
                                                       esp::EspRuntimeOwner& esp)
    : data_(std::make_unique<Data>(s, e, c, h, q, esp)) {}
PredictionMaskRuntimeOwner::~PredictionMaskRuntimeOwner() = default;
ClientRuntimeStorage& PredictionMaskRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
PredictionMaskFactoryBindings& PredictionMaskRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void PredictionMaskRuntimeOwner::bind_initializer(client::NativeStartupSources& sources) {
    bind_prediction_mask_factory(sources, data_->factory);
}
} //namespace skeet::rage
