#include "skeet/events/event_name_storage.hpp"
#include "skeet/events/registry.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/engine/preserved_initialization.hpp"
#include <stdexcept>
#include <utility>
#include <cstring>

namespace skeet::events {
namespace {
EventNameStartupBindings* active_names = nullptr;
// Constant source operands reconstruct the stack plaintext of43480770. They
// are used only by the real allocating constructors, never as consumer names.
constexpr std::array<const char*, EventNameStorage::count> initial_names{"pre_render",
                                                                         "pre_render_3d",
                                                                         "override_view",
                                                                         "string_cmd",
                                                                         "text",
                                                                         "pre_config_load",
                                                                         "post_config_load",
                                                                         "pre_config_save",
                                                                         "post_config_save",
                                                                         "send_datagram",
                                                                         "send_net_msg",
                                                                         "player_chat",
                                                                         "teamonly",
                                                                         "entity",
                                                                         "name",
                                                                         "server_cvar",
                                                                         "cvarname",
                                                                         "cvarvalue",
                                                                         "invalid event name",
                                                                         "hitgroup",
                                                                         "damage",
                                                                         "hit_chance",
                                                                         "boosted",
                                                                         "high_priority",
                                                                         "aim_fire",
                                                                         "aim_hit",
                                                                         "aim_miss",
                                                                         "console_input",
                                                                         "net_update_start",
                                                                         "net_update_end",
                                                                         "level_init",
                                                                         "run_command",
                                                                         "finish_command",
                                                                         "chokedcommands",
                                                                         "command_number",
                                                                         "post_render",
                                                                         "paint",
                                                                         "paint_ui",
                                                                         "pre_predict_command",
                                                                         "predict_command",
                                                                         "setup_command",
                                                                         "voice"};
std::size_t row_index(EventName name) {
    const auto address = static_cast<std::uint32_t>(name);
    const auto offset = address - EventNameStorage::native_base;
    if ((offset & 15U) || offset >= EventNameStorage::count * 16U)
        throw std::out_of_range("Name is not a row of native43479360 storage");
    return offset / 16U;
}
void finalize_names() {
    active_names->finalize();
}
void initialize_names(void* owner) { //4331EDEC
    auto& source = *static_cast<EventNameStartupBindings*>(owner);
    source.initialize(); //CALL43480770, before the finalizer registration
    (void)source.callbacks.append(source.source_finalizer(0x43419B17u)); //JMP433837F5
}
EventNameStorage::Cells& image_cells(const std::shared_ptr<engine::NativeDataImage>& image) {
    if (!image)
        throw std::invalid_argument("Event names require their native data owner");
    return image->native_cell<EventNameStorage::Cells>(EventNameStorage::native_base);
}
}
EventNameStorage::EventNameStorage(engine::PreservedInitialization& source)
    : EventNameStorage(source.image_owner()) {}
EventNameStorage::EventNameStorage(std::shared_ptr<engine::NativeDataImage> image)
    : image_(std::move(image)), cells(image_cells(image_)) {}
const engine::NativeDataImage& EventNameStorage::image() const {
    if (!image_)
        throw std::logic_error("Snapshot event names have no native data owner");
    return *image_;
}
misc::NativeText& EventNameStorage::at(EventName name) {
    return cells[row_index(name)];
}
const misc::NativeText& EventNameStorage::at(EventName name) const {
    return cells[row_index(name)];
}
StandaloneEventNameStorage::StandaloneEventNameStorage() noexcept : view_(cells_) {
    // Isolated consumer adapters own their mutable backing explicitly. This
    // never runs for the production image view or passes its startup guard.
    for (std::size_t row = 0; row < cells_.size(); ++row) {
        const auto length = std::strlen(initial_names[row]);
        auto* const bytes = backing_[row].data();
        std::memcpy(bytes, initial_names[row], length + 1);
        cells_[row] = {bytes, bytes + length, bytes + length + 1, 0};
    }
}
EventNameStorage& initialize_event_name_storage(EventNameStorage& names, engine::JobMemory memory) {
    //433B2213 clears only begin/end/limit, preserves each fourth word, scans
    // the exact literal and allocates/copies its own writable native backing.
    for (std::size_t row = 0; row < names.cells.size(); ++row)
        misc::construct_native_text(names.cells[row], initial_names[row], memory);
    return names; //4348199A returns the first native row,43479360
}
void finalize_event_name_storage(EventNameStorage& names, engine::JobMemory memory) {
    //4332B67C starts at+290 and ends with a tail call for+0. It neither clears
    // the freed headers nor edits the listener registry or its dirty byte.
    for (std::size_t row = names.cells.size(); row != 0; --row)
        misc::destroy_native_text(names.cells[row - 1], memory);
}
void EventNameStartupBindings::initialize() {
    initialize_event_name_storage(registry.name_storage, memory);
}
void EventNameStartupBindings::finalize() {
    finalize_event_name_storage(registry.name_storage, memory);
}
void (*EventNameStartupBindings::source_finalizer(std::uint32_t address))() {
    if (address != 0x43419B17u)
        throw std::out_of_range("Finalizer is not the native event-name cleanup entry");
    if (active_names && active_names != this)
        throw std::logic_error("Event-name exit callback already has another stable context");
    active_names = this;
    return finalize_names;
}
void bind_event_name_initializer(client::NativeStartupSources& sources,
                                 EventNameStartupBindings& bindings) {
    if (&bindings.registry.name_storage.image() != &sources.data())
        throw std::invalid_argument(
            "Event-name CRT and registry require the same canonical native data owner");
    sources.bind_initializer({0x4331EDECu, &bindings, initialize_names});
}
} // namespace skeet::events
