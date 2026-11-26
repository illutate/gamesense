#include "skeet/core.hpp"
#include <bit>
#include <stdexcept>

namespace skeet {
namespace {
std::int32_t wrapping_sum(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
}

// Original complete function: 0x433EB3B9..0x433EB3E9.
bool contains(const Rectangle& bounds, Point cursor) noexcept {
    return cursor.x >= bounds.x && cursor.x <= wrapping_sum(bounds.x, bounds.width) &&
           cursor.y >= bounds.y && cursor.y <= wrapping_sum(bounds.y, bounds.height);
}

// Original complete function: 0x433B75D3..0x433B760B.
void select_tab(Menu& menu, std::uint32_t requested_tab) {
    if (requested_tab == menu.active_tab || requested_tab >= menu.tabs.size())
        return;
    if (menu.active_tab >= menu.tabs.size())
        throw std::out_of_range("Menu active tab is outside the tab vector");
    auto& previous = menu.tabs[menu.active_tab];
    previous.reset(previous.object);
    // Reset may change the vector. Reload the requested tab after it returns.
    menu.active_tab = requested_tab;
    auto& next = menu.tabs[requested_tab];
    next.activate(next.object);
}
} // namespace skeet
