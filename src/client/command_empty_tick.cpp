#include "skeet/client/command_empty_tick.hpp"
#include "skeet/client/command_owner_dispatch.hpp"
#include "skeet/misc/grenade_command.hpp"
#include "skeet/effects/bullet_impact_command_owner.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include "skeet/engine/unload.hpp"
#include <stdexcept>

#if defined(_M_IX86) || defined(__i386__)
#define EMPTY_TICK_ENTRY __vectorcall
#else
#define EMPTY_TICK_ENTRY
#endif

namespace skeet::client {
namespace {
void EMPTY_TICK_ENTRY prediction_entry(NativeCommandEmptyTickReceiver* receiver, void* player,
                                       float, Command*, std::uint8_t*) {
    apply_empty_prediction_history(*static_cast<PredictionHistoryOwner*>(receiver->source), player);
}
void EMPTY_TICK_ENTRY legit_entry(NativeCommandEmptyTickReceiver* receiver, void*,
                                  float sample_time, Command* command, std::uint8_t*) {
    auto& owner = *static_cast<LegitCommandOwner*>(receiver->source);
    owner.pending(*command, sample_time);
}
void EMPTY_TICK_ENTRY timer_entry(NativeCommandEmptyTickReceiver* receiver, void*,
                                  float sample_time, Command*, std::uint8_t*) {
    auto& timer = *static_cast<CommandEmptyTickTimer*>(receiver->source);
    timer.remaining = maximum_number(timer.remaining - sample_time, 0.0f);
}
void EMPTY_TICK_ENTRY grenade_entry(NativeCommandEmptyTickReceiver* receiver, void* player, float,
                                    Command* command, std::uint8_t*) {
    static_cast<misc::GrenadeCommandRuntime*>(receiver->source)
        ->observer0(player, *command, *receiver->input);
}
void EMPTY_TICK_ENTRY camera_entry(NativeCommandEmptyTickReceiver* receiver, void* player, float,
                                   Command*, std::uint8_t*) {
    static_cast<effects::EffectsCommandOwner*>(receiver->source)->camera.update_observer(player);
}
const NativeCommandEmptyTickVtable prediction_table{prediction_entry}; //43470470
const NativeCommandEmptyTickVtable legit_table{legit_entry}; //434704B4
const NativeCommandEmptyTickVtable timer_table{timer_entry}; //4346F3B0
const NativeCommandEmptyTickVtable grenade_table{grenade_entry}; //434704BC
const NativeCommandEmptyTickVtable camera_table{camera_entry}; //4347270C
NativeCommandEmptyTickReceiver& source_receiver(void* object) {
    auto* receiver = static_cast<NativeCommandEmptyTickReceiver*>(object);
    if (!receiver || !receiver->source ||
        (receiver->vtable != &prediction_table && receiver->vtable != &legit_table &&
         receiver->vtable != &timer_table && receiver->vtable != &grenade_table &&
         receiver->vtable != &camera_table))
        throw std::logic_error("Native empty-tick entry has no concrete source receiver binding");
    return *receiver;
}
}
CommandEmptyTickRegistry::CommandEmptyTickRegistry(engine::PreservedInitialization& initialization,
                                                   engine::JobMemory memory,
                                                   const InputBitmap& input)
    : CommandEmptyTickRegistry(initialization.image_owner(), memory, input) {}
CommandEmptyTickRegistry::CommandEmptyTickRegistry(std::shared_ptr<engine::NativeDataImage> image,
                                                   engine::JobMemory memory,
                                                   const InputBitmap& input)
    : image_(image),
      owners_(engine::require_native_data_image(image_).native_cell<engine::OwnerRegistry>(
          0x43475978u)),
      memory_(memory), input_(input) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument(
            "Native empty-tick storage requires the canonical process heap");
}
void CommandEmptyTickRegistry::append_receiver(NativeCommandEmptyTickReceiver& receiver,
                                               void* source) {
    receiver.source = source;
    receiver.input = &input_;
    //433F10DB shares the -4 sentinel and433F1665/4334DC99 append/growth.
    //Final table publication belongs to the actual later constructor cut.
    engine::register_owner(owners_, &receiver, memory_);
}
void CommandEmptyTickRegistry::append(PredictionHistoryOwner& owner) {
    append_receiver(owner.empty_tick_receiver, &owner);
}
void CommandEmptyTickRegistry::append(LegitCommandOwner& owner) {
    append_receiver(owner.empty_tick_receiver, &owner);
}
void CommandEmptyTickRegistry::append(CommandEmptyTickTimer& owner) {
    append_receiver(owner.empty_tick_receiver, &owner);
}
void CommandEmptyTickRegistry::append_grenade(NativeCommandEmptyTickReceiver& receiver,
                                              misc::GrenadeCommandRuntime* owner) {
    append_receiver(receiver, owner);
}
void CommandEmptyTickRegistry::append(effects::EffectsCommandOwner& owner) {
    append_receiver(owner.empty_tick_receiver, &owner);
}
void CommandEmptyTickRegistry::publish(PredictionHistoryOwner& owner) const noexcept {
    owner.empty_tick_receiver.vtable = &prediction_table;
}
void CommandEmptyTickRegistry::publish(LegitCommandOwner& owner) const noexcept {
    owner.empty_tick_receiver.vtable = &legit_table;
}
void CommandEmptyTickRegistry::publish(CommandEmptyTickTimer& owner) const noexcept {
    owner.empty_tick_receiver.vtable = &timer_table;
}
void CommandEmptyTickRegistry::publish_grenade(
    NativeCommandEmptyTickReceiver& receiver) const noexcept {
    receiver.vtable = &grenade_table;
}
void CommandEmptyTickRegistry::publish(effects::EffectsCommandOwner& owner) const noexcept {
    owner.empty_tick_receiver.vtable = &camera_table;
}
void CommandEmptyTickRegistry::require_sources() const {
    for (auto* object : owners_.entries())
        (void)source_receiver(object);
}
void CommandEmptyTickRegistry::dispatch(void* player, float sample_time, Command& command,
                                        std::uint8_t& result) const {
    //433D2F59 loads end, then433D2F5F loads begin, once before any callback.
    auto** const end = owners_.end;
    auto** cursor = owners_.begin;
    for (; cursor != end; ++cursor) {
        auto& receiver = source_receiver(*cursor);
        receiver.vtable->invoke(&receiver, player, sample_time, &command, &result);
    }
}
void CommandEmptyTickRegistry::finalize_storage() const {
    engine::UnloadSystemFunctions memory;
    memory.free_heap = memory_.free;
    memory.process_heap = memory_.process_heap;
    engine::free_native_allocation(owners_.begin, memory); //43349ECB, no cell reset
}
void require_empty_tick_sources(const CommandEmptyTickRegistry& registry) {
    registry.require_sources();
}
void dispatch_empty_tick_command(const CommandEmptyTickRegistry& registry, void* player,
                                 float sample_time, Command& command, std::uint8_t& result) {
    registry.dispatch(player, sample_time, command, result);
}
} //namespace skeet::client

#undef EMPTY_TICK_ENTRY
