#include "skeet/render/menu_window_assets.hpp"
#include "skeet/render/menu_background.hpp"
#include "skeet/render/backend.hpp"
#include "skeet/menu_window.hpp"
#include <array>
#include <span>

namespace skeet::ui {
namespace {
// Decoded resource25D9, preserved byte-for-byte from skeet/Binary/menu.h.
// The supplied capture resumes43493908 at434801F2 with EAX1 and size12000.
#include "menu_tab_atlas.inc"
static_assert(sizeof(menu_tab_atlas_pixels) == 384 * 48 * 4);
}
std::uint32_t create_menu_tab_atlas(RendererBackend& renderer) { //434801C9, known resource
    return renderer.create_texture(0x08f6352a, 384, 48,
                                   std::as_bytes(std::span(menu_tab_atlas_pixels)));
}
void initialize_main_menu_assets(MenuWindow& window, RendererBackend& renderer,
                                 MainMenuAssets& assets) { //43485A05
    assets.tab_atlas = create_menu_tab_atlas(renderer);
    assign_menu_background(window, renderer);
    if (assets.tab_atlas == UINT32_MAX)
        return;
    // Native pointer order+B4,+CC,+B8,+BC,+C0,+C4,+C8,+D0.
    constexpr std::array<std::int32_t, 8> source_x{0, 288, 48, 96, 144, 192, 240, 336};
    for (std::size_t i = 0; i < source_x.size(); ++i) {
        auto& tab = *window.tabs.at(i);
        tab.icon = assets.tab_atlas;
        tab.icon_source = {source_x[i], 0, 48, 48};
    }
}
}
