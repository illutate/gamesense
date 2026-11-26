#include "skeet/client/entity_observer_registry.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include "skeet/client/entity_command_owner.hpp"
#include "skeet/client/target_mask_owner.hpp"
#include "skeet/client/command_owner_dispatch.hpp"
#include "skeet/lagcomp/history_receiver.hpp"
#include "skeet/misc/clan_tag.hpp"
#include "skeet/misc/viewmodel_lag.hpp"
#include "skeet/misc/grenade_command.hpp"
#include "skeet/prediction/observer_registry.hpp"
#include "skeet/prediction/entity_update.hpp"
#include "skeet/esp/sound_overlay.hpp"
#include "skeet/skins/native_entity_cosmetics.hpp"
#include "skeet/skins/native_inventory.hpp"
#include "skeet/skins/native_sequence_update.hpp"
#include "skeet/skins/sequence_owner.hpp"
#include "skeet/effects/bullet_impact_command_owner.hpp"
#include "skeet/script/entity_observer.hpp"
#include "skeet/rage/entity_update.hpp"
#include "skeet/rage/feedback_entity_owner.hpp"
#include "skeet/engine/unload.hpp"
#include <cstring>
#include <stdexcept>

#if defined(_M_IX86) || defined(__i386__)
#define ENTITY_OBSERVER_FASTCALL __fastcall
#else
#define ENTITY_OBSERVER_FASTCALL
#endif

namespace skeet::client {
namespace {
constexpr std::array<EntityObserverTable, 18> tables{{
    {EntityObserverId::prediction_history, {{0x433CCE8Eu, 0x43397E0Du, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::clan_tag, {{0x433CCE8Eu, 0x433C95B7u, 0x433CCE8Eu, 0x433414C7u}}},
    {EntityObserverId::history_recording, {{0x433CCE8Eu, 0x433C95B7u, 0x433CCE8Eu, 0x4340C971u}}},
    {EntityObserverId::mask, {{0x433CCE8Eu, 0x43408846u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::shot_feedback, {{0x433CCE8Eu, 0x433C95B7u, 0x4333D358u, 0x433240DDu}}},
    {EntityObserverId::entity_commands, {{0x433CCE8Eu, 0x433CFDA2u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::scripts, {{0x433CCE8Eu, 0x433998EBu, 0x433CCE8Eu, 0x4336A3FDu}}},
    {EntityObserverId::rage, {{0x433CCE8Eu, 0x43325DECu, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::target_mask, {{0x4336AC2Eu, 0x4336526Du, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::pickup_history, {{0x433CCE8Eu, 0x433C95B7u, 0x433CCE8Eu, 0x433E0F98u}}},
    {EntityObserverId::prediction_update, {{0x433CCE8Eu, 0x433C95B7u, 0x433CCE8Eu, 0x4337D273u}}},
    {EntityObserverId::shared_esp, {{0x433CCE8Eu, 0x433C95B7u, 0x433654AAu, 0x43382555u}}},
    {EntityObserverId::grenades, {{0x433CCE8Eu, 0x43337BC0u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::viewmodel_lag, {{0x433CCE8Eu, 0x433833B0u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::sequences, {{0x433CCE8Eu, 0x433E53A5u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::effects, {{0x433CCE8Eu, 0x43326129u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::inventory, {{0x433CCE8Eu, 0x4339D6B2u, 0x433CCE8Eu, 0x433C95B7u}}},
    {EntityObserverId::gloves, {{0x433CCE8Eu, 0x43342310u, 0x433CCE8Eu, 0x433C95B7u}}},
}};
const std::array<std::uint32_t, 4>& methods(EntityObserverId id) {
    for (const auto& table : tables)
        if (table.id == id)
            return table.methods;
    throw std::invalid_argument("Unknown native entity observer");
}
bool noop(std::uint32_t entry) noexcept {
    return entry == 0x433CCE8Eu || entry == 0x433C95B7u;
}
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> T& source(void* object) {
    auto* base = static_cast<NativeEntityObserver*>(object);
    return *static_cast<T*>(static_cast<NativeEntityObserverReceiver*>(base)->source);
}
template <class T> T& existing(void* object) {
    return *static_cast<T*>(static_cast<NativeEntityObserver*>(object));
}
void ENTITY_OBSERVER_FASTCALL empty_status(void*, const void*) {} //433CCE8E
void ENTITY_OBSERVER_FASTCALL empty_update(void*, void*, std::uintptr_t) {} //433C95B7
void ENTITY_OBSERVER_FASTCALL empty_reset(void*, std::int32_t) {} //433CCE8E
void ENTITY_OBSERVER_FASTCALL empty_incoming(void*, void*, std::int32_t) {} //433C95B7
void ENTITY_OBSERVER_FASTCALL update_prediction(void* object, void* player, std::uintptr_t) {
    observe_prediction_history(source<PredictionHistoryOwner>(object), player);
}
void ENTITY_OBSERVER_FASTCALL incoming_clan(void* object, void*, std::int32_t) {
    source<misc::ClanTagRuntime>(object).update();
}
void ENTITY_OBSERVER_FASTCALL incoming_history(void* object, void* player, std::int32_t) {
    auto& owner = existing<lagcomp::HistoryRecordingReceiver>(object);
    owner.record_players(player);
}
void ENTITY_OBSERVER_FASTCALL update_mask(void* object, void* player, std::uintptr_t) {
    source<skins::NativeEntityCosmetics>(object).update_mask(player);
}
void ENTITY_OBSERVER_FASTCALL reset_feedback(void* object, std::int32_t) {
    rage::reset_feedback_events(source<rage::FeedbackEntityOwner>(object));
}
void ENTITY_OBSERVER_FASTCALL incoming_feedback(void* object, void*, std::int32_t) {
    rage::reconcile_feedback_events(source<rage::FeedbackEntityOwner>(object));
}
void ENTITY_OBSERVER_FASTCALL update_entity_commands(void* object, void*, std::uintptr_t incoming) {
    process_entity_prediction_entities(
        source<EntityCommandOwner>(object),
        *reinterpret_cast<const EntityPredictionEntities*>(incoming));
}
void ENTITY_OBSERVER_FASTCALL update_scripts(void* object, void* player, std::uintptr_t incoming) {
    source<script::ScriptEntityObserver>(object).update_entities(player, incoming);
}
void ENTITY_OBSERVER_FASTCALL incoming_scripts(void* object, void* player, std::int32_t delta) {
    source<script::ScriptEntityObserver>(object).incoming_complete(player, delta);
}
void ENTITY_OBSERVER_FASTCALL update_rage(void* object, void* player, std::uintptr_t incoming) {
    rage::update_rage_entities(source<const rage::EntityUpdateBindings>(object), player,
                               *reinterpret_cast<const EntityPredictionEntities*>(incoming));
}
void ENTITY_OBSERVER_FASTCALL status_target_mask(void* object, const void* status) {
    observe_target_mask_update_status(source<TargetMaskOwner>(object), status);
}
void ENTITY_OBSERVER_FASTCALL update_target_mask(void* object, void*, std::uintptr_t incoming) {
    capture_target_mask_update(source<TargetMaskOwner>(object), incoming);
}
void ENTITY_OBSERVER_FASTCALL incoming_sound(void* object, void*, std::int32_t) {
    existing<esp::SoundHistoryReceiver>(object).process_incoming();
}
void ENTITY_OBSERVER_FASTCALL incoming_prediction(void* object, void*, std::int32_t) {
    prediction::update_prediction_entities(source<prediction::EngineEntityUpdateOwner>(object));
}
void ENTITY_OBSERVER_FASTCALL reset_shared(void* object, std::int32_t) {
    enter_shared_esp_update(source<SharedEspCommandOwner>(object).bindings);
}
void ENTITY_OBSERVER_FASTCALL incoming_shared(void* object, void*, std::int32_t) {
    leave_shared_esp_update(source<SharedEspCommandOwner>(object).bindings);
}
void ENTITY_OBSERVER_FASTCALL update_grenades(void* object, void* player, std::uintptr_t incoming) {
    source<misc::GrenadeCommandRuntime>(object).update_remote_warnings(player, incoming);
}
void ENTITY_OBSERVER_FASTCALL update_viewmodel(void* object, void* player, std::uintptr_t) {
    auto& owner = source<prediction::ViewmodelPredictionObserver>(object);
    misc::repair_viewmodel_lag(owner.state, owner.bindings, player);
}
void ENTITY_OBSERVER_FASTCALL update_sequences(void* object, void* player,
                                               std::uintptr_t incoming) {
    existing<skins::SequenceEntityObserverReceiver>(object).runtime->process(player, incoming);
}
void ENTITY_OBSERVER_FASTCALL update_effects(void* object, void* player, std::uintptr_t incoming) {
    effects::update_effects_entities(source<effects::EffectsCommandOwner>(object), player,
                                     incoming);
}
void ENTITY_OBSERVER_FASTCALL update_inventory(void* object, void* player,
                                               std::uintptr_t incoming) {
    source<skins::NativeInventoryRuntime>(object).process(player, incoming);
}
void ENTITY_OBSERVER_FASTCALL update_gloves(void* object, void* player, std::uintptr_t) {
    source<skins::NativeEntityCosmetics>(object).update_gloves(player);
}
const NativeEntityObserverVtable prediction_table{empty_status, update_prediction, empty_reset,
                                                  empty_incoming};
const NativeEntityObserverVtable clan_table{empty_status, empty_update, empty_reset, incoming_clan};
const NativeEntityObserverVtable history_table{empty_status, empty_update, empty_reset,
                                               incoming_history};
const NativeEntityObserverVtable mask_table{empty_status, update_mask, empty_reset, empty_incoming};
const NativeEntityObserverVtable feedback_table{empty_status, empty_update, reset_feedback,
                                                incoming_feedback};
const NativeEntityObserverVtable entity_table{empty_status, update_entity_commands, empty_reset,
                                              empty_incoming};
const NativeEntityObserverVtable script_table{empty_status, update_scripts, empty_reset,
                                              incoming_scripts};
const NativeEntityObserverVtable rage_table{empty_status, update_rage, empty_reset, empty_incoming};
const NativeEntityObserverVtable target_table{status_target_mask, update_target_mask, empty_reset,
                                              empty_incoming};
const NativeEntityObserverVtable sound_table{empty_status, empty_update, empty_reset,
                                             incoming_sound};
const NativeEntityObserverVtable engine_prediction_table{empty_status, empty_update, empty_reset,
                                                         incoming_prediction};
const NativeEntityObserverVtable shared_table{empty_status, empty_update, reset_shared,
                                              incoming_shared};
const NativeEntityObserverVtable grenade_table{empty_status, update_grenades, empty_reset,
                                               empty_incoming};
const NativeEntityObserverVtable viewmodel_table{empty_status, update_viewmodel, empty_reset,
                                                 empty_incoming};
const NativeEntityObserverVtable sequence_table{empty_status, update_sequences, empty_reset,
                                                empty_incoming};
const NativeEntityObserverVtable effects_table{empty_status, update_effects, empty_reset,
                                               empty_incoming};
const NativeEntityObserverVtable inventory_table{empty_status, update_inventory, empty_reset,
                                                 empty_incoming};
const NativeEntityObserverVtable gloves_table{empty_status, update_gloves, empty_reset,
                                              empty_incoming};
constexpr std::array<const NativeEntityObserverVtable*, 18> source_tables{&prediction_table,
                                                                          &clan_table,
                                                                          &history_table,
                                                                          &mask_table,
                                                                          &feedback_table,
                                                                          &entity_table,
                                                                          &script_table,
                                                                          &rage_table,
                                                                          &target_table,
                                                                          &sound_table,
                                                                          &engine_prediction_table,
                                                                          &shared_table,
                                                                          &grenade_table,
                                                                          &viewmodel_table,
                                                                          &sequence_table,
                                                                          &effects_table,
                                                                          &inventory_table,
                                                                          &gloves_table};
NativeEntityObserver& source_receiver(void* object) {
    auto* receiver = static_cast<NativeEntityObserver*>(object);
    if (receiver)
        for (const auto* table : source_tables)
            if (receiver->vtable == table)
                return *receiver;
    throw std::logic_error("Native entity observer has no concrete source receiver binding");
}
}
std::span<const EntityObserverTable> native_entity_observer_tables() noexcept {
    return tables;
}
std::optional<EntityObserverId> identify_entity_observer(std::uint32_t address) noexcept {
    for (const auto& table : tables)
        if (static_cast<std::uint32_t>(table.id) == address)
            return table.id;
    return std::nullopt;
}
bool entity_observer_source_bound(EntityObserverId id, std::uint32_t slot) {
    if (slot >= 4)
        throw std::invalid_argument("Invalid entity observer slot");
    const auto entry = methods(id)[slot];
    if (noop(entry))
        return true;
    switch (entry) {
    case 0x43397E0D:
    case 0x43408846:
    case 0x433CFDA2:
    case 0x4336AC2E:
    case 0x4336526D:
    case 0x433833B0:
    case 0x43326129:
    case 0x4339D6B2:
    case 0x43342310:
    case 0x433E53A5:
    case 0x433414C7:
    case 0x4340C971:
    case 0x433E0F98:
        return true;
    case 0x433998EB:
    case 0x4336A3FD:
    case 0x433654AA:
    case 0x43382555:
        return true;
    case 0x4337D273:
    case 0x43325DEC:
    case 0x4333D358:
    case 0x433240DD:
        return true;
    case 0x43337BC0:
        return true;
    default:
        return false;
    }
}
EntityObserverRegistry::EntityObserverRegistry(engine::PreservedInitialization& initialization,
                                               engine::JobMemory memory)
    : EntityObserverRegistry(initialization.image_owner(), memory) {}
EntityObserverRegistry::EntityObserverRegistry(std::shared_ptr<engine::NativeDataImage> image,
                                               engine::JobMemory memory)
    : image_(image),
      owners_(engine::require_native_data_image(image_).native_cell<engine::OwnerRegistry>(
          0x43475BA8u)),
      memory_(memory) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Native entity observers require the canonical process heap");
}
void EntityObserverRegistry::append_native(NativeEntityObserver& receiver) {
    engine::register_owner(owners_, &receiver, memory_); //4333BF32, including its -4 sentinel
}
void EntityObserverRegistry::append_receiver(NativeEntityObserverReceiver& receiver, void* owner) {
    receiver.source = owner;
    append_native(receiver);
}
void EntityObserverRegistry::append_prediction_history(NativeEntityObserverReceiver& receiver,
                                                       PredictionHistoryOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_clan_tag(NativeEntityObserverReceiver& receiver,
                                             misc::ClanTagRuntime& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_mask(NativeEntityObserverReceiver& receiver,
                                         skins::NativeEntityCosmetics& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_feedback(NativeEntityObserverReceiver& receiver,
                                             rage::FeedbackEntityOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_entity_commands(NativeEntityObserverReceiver& receiver,
                                                    EntityCommandOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_scripts(NativeEntityObserverReceiver& receiver,
                                            script::ScriptEntityObserver& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_rage(NativeEntityObserverReceiver& receiver,
                                         const rage::EntityUpdateBindings& owner) {
    append_receiver(receiver, const_cast<rage::EntityUpdateBindings*>(&owner));
}
void EntityObserverRegistry::append_target_mask(NativeEntityObserverReceiver& receiver,
                                                TargetMaskOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_prediction_update(NativeEntityObserverReceiver& receiver,
                                                      prediction::EngineEntityUpdateOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_shared_esp(NativeEntityObserverReceiver& receiver,
                                               SharedEspCommandOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_grenades(NativeEntityObserverReceiver& receiver,
                                             misc::GrenadeCommandRuntime* owner) {
    append_receiver(receiver, owner);
}
void EntityObserverRegistry::append_viewmodel_lag(NativeEntityObserverReceiver& receiver,
                                                  prediction::ViewmodelPredictionObserver& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_effects(NativeEntityObserverReceiver& receiver,
                                            effects::EffectsCommandOwner& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_inventory(NativeEntityObserverReceiver& receiver,
                                              skins::NativeInventoryRuntime& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append_gloves(NativeEntityObserverReceiver& receiver,
                                           skins::NativeEntityCosmetics& owner) {
    append_receiver(receiver, &owner);
}
void EntityObserverRegistry::append(lagcomp::HistoryRecordingReceiver& owner) {
    append_native(owner);
    lagcomp::bind_history_recording_runtime(*owner.runtime);
}
void EntityObserverRegistry::append(esp::SoundHistoryReceiver& owner) {
    append_native(owner);
}
void EntityObserverRegistry::append(skins::SequenceEntityObserverReceiver& receiver,
                                    skins::NativeSequenceRuntime& owner) {
    receiver.runtime = &owner;
    append_native(receiver);
}
void EntityObserverRegistry::publish_prediction_history(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &prediction_table;
}
void EntityObserverRegistry::publish_clan_tag(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &clan_table;
}
void EntityObserverRegistry::publish_mask(NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &mask_table;
}
void EntityObserverRegistry::publish_feedback(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &feedback_table;
}
void EntityObserverRegistry::publish_entity_commands(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &entity_table;
}
void EntityObserverRegistry::publish_scripts(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &script_table;
}
void EntityObserverRegistry::publish_rage(NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &rage_table;
}
void EntityObserverRegistry::publish_target_mask(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &target_table;
}
void EntityObserverRegistry::publish_prediction_update(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &engine_prediction_table;
}
void EntityObserverRegistry::publish_shared_esp(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &shared_table;
}
void EntityObserverRegistry::publish_grenades(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &grenade_table;
}
void EntityObserverRegistry::publish_viewmodel_lag(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &viewmodel_table;
}
void EntityObserverRegistry::publish_effects(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &effects_table;
}
void EntityObserverRegistry::publish_inventory(
    NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &inventory_table;
}
void EntityObserverRegistry::publish_gloves(NativeEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &gloves_table;
}
void EntityObserverRegistry::publish(lagcomp::HistoryRecordingReceiver& owner) const noexcept {
    owner.vtable = &history_table;
}
void EntityObserverRegistry::publish(esp::SoundHistoryReceiver& owner) const noexcept {
    owner.vtable = &sound_table;
}
void EntityObserverRegistry::publish(
    skins::SequenceEntityObserverReceiver& receiver) const noexcept {
    receiver.vtable = &sequence_table;
}
void EntityObserverRegistry::require_sources(std::uint32_t slot) const {
    if (slot >= 4)
        throw std::invalid_argument("Invalid entity observer slot");
    for (auto* object : owners())
        (void)source_receiver(object);
}
void EntityObserverRegistry::update_status(const void* status) const {
    auto** const end = owners_.end; //4333CA25
    auto** cursor = owners_.begin; //4333CA2B
    for (; cursor != end; ++cursor)
        source_receiver(*cursor).vtable->status(*cursor, status);
}
void EntityObserverRegistry::update_entities(void* player, std::uintptr_t incoming) const {
    auto** const end = owners_.end; //433206AD
    auto** cursor = owners_.begin; //433206B3
    for (; cursor != end; ++cursor)
        source_receiver(*cursor).vtable->update(*cursor, player, incoming);
}
void EntityObserverRegistry::reset_update_state(std::int32_t selected_tick) const {
    auto** const end = owners_.end; //4341F019
    auto** cursor = owners_.begin; //4341F01F
    for (; cursor != end; ++cursor)
        source_receiver(*cursor).vtable->reset(*cursor, selected_tick);
}
void EntityObserverRegistry::incoming_complete(void* player, std::int32_t delta) const {
    auto** const end = owners_.end; //433CFAEA
    auto** cursor = owners_.begin; //433CFAF0
    for (; cursor != end; ++cursor)
        source_receiver(*cursor).vtable->incoming(*cursor, player, delta);
}
void EntityObserverRegistry::finalize_storage() const {
    engine::UnloadSystemFunctions memory;
    memory.free_heap = memory_.free;
    memory.process_heap = memory_.process_heap;
    engine::free_native_allocation(owners_.begin, memory); //433E4C98, no cell reset
}
} //namespace skeet::client

#undef ENTITY_OBSERVER_FASTCALL
