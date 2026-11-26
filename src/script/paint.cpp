#include "skeet/script/paint.hpp"

namespace skeet::script {
namespace {
int marshal_paint(Bridge& bridge, void*) { // 43388E2D
    bridge.api->pushnil(bridge.state);
    return 1;
}
int marshal_paint_ui(Bridge&, void*) { // 43365AE1 ->43336942
    return 0;
}
}

void dispatch_paint(Bridge& bridge, ui::RenderQueue& queue, std::int32_t& cursor) { // 433870EE
    if (!bridge.state)
        return;
    bridge.render_context = {&queue, &cursor};
    dispatch_callbacks(bridge, bridge.name(events::EventName::paint).get(), marshal_paint, nullptr);
    // Both native dispatchers clear the shared context rather than restoring
    // an outer callback's queue after a nested dispatch.
    bridge.render_context = {};
}

void dispatch_paint_ui(Bridge& bridge, ui::RenderQueue& queue, std::int32_t& cursor) { // 4336F85E
    if (!bridge.state)
        return;
    bridge.render_context = {&queue, &cursor};
    dispatch_callbacks(bridge, bridge.name(events::EventName::paint_ui).get(), marshal_paint_ui,
                       nullptr);
    bridge.render_context = {};
}

const hud::HudFrameEntry PaintHudHandler::entries_[1]{paint_entry};

PaintHudHandler::PaintHudHandler(Bridge& bridge, engine::OwnerRegistryBinding callbacks)
    : bridge_(bridge) {
    hud::register_hud_frame(callbacks, this);
}

void PaintHudHandler::publish_table() noexcept {
    vtable = entries_; // native4346FD9C
}

#if defined(_M_IX86) || defined(__i386__)
void __fastcall PaintHudHandler::paint_entry(void* object, ui::RenderQueue* queue, void*,
                                             std::int32_t* cursor) {
#else
void PaintHudHandler::paint_entry(void* object, ui::RenderQueue* queue, void*,
                                  std::int32_t* cursor) {
#endif
    auto& owner = *static_cast<PaintHudHandler*>(static_cast<hud::HudFrameCallback*>(object));
    dispatch_paint(owner.bridge_, *queue, *cursor);
}

} // namespace skeet::script
