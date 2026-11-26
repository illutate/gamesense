#include "skeet/esp/frame_handler.hpp"

namespace skeet::esp {
EspFrameHandler::EspFrameHandler(engine::OwnerRegistryBinding registry, EspFrameOwner& owner,
                                 const EspFrameContext& context)
    : owner_(owner), context_(context) {
    // Native registration precedes installation of this interface's vtable.
    hud::register_hud_frame(registry, this);
    vtable = entries_;
}

#if defined(_M_IX86) || defined(__i386__)
void __fastcall EspFrameHandler::draw(void* object, ui::RenderQueue* queue, void*,
                                      std::int32_t* cursor) {
#else
void EspFrameHandler::draw(void* object, ui::RenderQueue* queue, void*, std::int32_t* cursor) {
#endif
    auto& handler = *static_cast<EspFrameHandler*>(static_cast<hud::HudFrameCallback*>(object));
    //43408EC9 resolves43359AFA itself; the dispatcher's local-player argument
    // is passed by the interface ABI but is not read by the native body.
    draw_esp_frame(*queue, handler.owner_, *cursor, handler.context_);
}
} // namespace skeet::esp
