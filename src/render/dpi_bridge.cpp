#include "skeet/render/dpi_bridge.hpp"
#include "skeet/menu_window.hpp"
#include "skeet/render/backend_container.hpp"

namespace skeet::ui {
RendererDpiBridge::RendererDpiBridge(GuiDpiState& dpi, RendererContainer& renderer) noexcept
    : dpi_(dpi), renderer_(renderer) {
    dpi_.font_resources = {this, refresh_bound_renderer};
}
RendererDpiBridge::~RendererDpiBridge() {
    if (dpi_.font_resources.context == this &&
        dpi_.font_resources.refresh == refresh_bound_renderer)
        dpi_.font_resources = {};
}
void RendererDpiBridge::refresh_bound_renderer(void* context) noexcept {
    static_cast<RendererDpiBridge*>(context)->refresh();
}
void RendererDpiBridge::refresh() noexcept {
    // 4339F0AD publishes4346D490 before front43472A80 slot80 forwards through
    // 4341ACC8 to backend4346F46C slot80 (433EA298, context+100 assignment).
    renderer_.set_font_scale(dpi_.active_scale);
}
}
