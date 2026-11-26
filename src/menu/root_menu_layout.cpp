#include "skeet/menu/root_menu.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::ui {
namespace {
std::uint32_t grid(const Category& category) noexcept {
    return std::uint8_t(category.column) |
           (std::uint32_t(std::uint8_t(category.column_span)) << 8) |
           (std::uint32_t(std::uint8_t(category.row)) << 16) |
           (std::uint32_t(std::uint8_t(category.row_span)) << 24);
}
template <class Value> Value read(const void* source, std::size_t offset) {
    Value value;
    std::memcpy(&value, static_cast<const std::byte*>(source) + offset, sizeof(value));
    return value;
}
}
Tab& append_menu_tab(MenuWindow& window, std::unique_ptr<Tab> owned) { // 0x43342275
    auto* tab = owned.get();
    tab->identity = static_cast<std::uint32_t>(window.tabs.size());
    window.tabs.push_back(tab);
    owned.release();
    tab->reflow_categories();
    tab->layout_categories();
    tab->reflow_categories();
    return *tab;
}
void capture_menu_layout(const RootMenuLayoutTargets& targets,
                         std::array<std::int32_t, 21>& destination, bool built_in,
                         std::array<std::uint32_t, 2>& player_defaults) { // 0x433ACDC7
    destination[0] = 9;
    constexpr std::array<std::size_t, 17> indices{1,  2,  3,  5,  4,  6,  7,  8, 9,
                                                  10, 11, 12, 13, 14, 15, 19, 20};
    for (std::size_t i = 0; i < indices.size(); ++i)
        destination[indices[i]] = std::bit_cast<std::int32_t>(grid(*targets.categories[i]));
    if (built_in) {
        player_defaults[0] = grid(*targets.players[0]);
        player_defaults[1] = grid(*targets.players[1]);
    }
}
void update_rage_hit_chance_visibility(Widget& hit_chance, void* engine_object,
                                       const void* accuracy_variable) { // 0x433815C4
    bool show = true;
    if (engine::invoke<bool>(engine_object, 26)) {
        auto* variable = accuracy_variable;
        for (;;) {
            const auto* parent = read<const void*>(variable, 0x1c);
            if (parent == variable)
                break;
            variable = parent;
        }
        show = read<std::uint32_t>(variable, 0x30) ==
               std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
    }
    hit_chance.set_visible(show);
}

}
