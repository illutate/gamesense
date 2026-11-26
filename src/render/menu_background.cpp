#include "skeet/render/menu_background.hpp"
#include "skeet/render/backend.hpp"
#include "skeet/menu_window.hpp"
#include <array>
#include <span>

namespace skeet::ui {
std::uint32_t create_menu_background(RendererBackend& renderer) {
    // Original stack array4348F902..4348F96B, copied as64bytes at4348F98C.
    //4341CC03 XORs native storage with its RDTSC key; RendererBackend's texture
    //records already keep decoded bytes for4336AFEE's D3D9 upload.
    static constexpr std::array<std::uint32_t, 16> pixels{
        0xff141414, 0xff141414, 0xff0c0c0c, 0xff141414, 0xff0c0c0c, 0xff141414,
        0xff0c0c0c, 0xff141414, 0xff0c0c0c, 0xff141414, 0xff141414, 0xff141414,
        0xff0c0c0c, 0xff141414, 0xff0c0c0c, 0xff141414,
    };
    return renderer.create_texture(menu_background_key, 4, 4, std::as_bytes(std::span(pixels)));
}
void assign_menu_background(MenuWindow& window, const RendererBackend& renderer) noexcept {
    window.background_texture =
        static_cast<std::uint32_t>(renderer.find_texture(menu_background_key));
}
}
