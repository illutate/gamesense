#include "skeet/menu/rage_menu.hpp"
#include <algorithm>
#include <stdexcept>

namespace skeet::ui {
RageWeaponMenu::RageWeaponMenu(Tab& tab, std::uint32_t& selected_group,
                               std::span<std::uint8_t> enabled_groups,
                               RageWeaponSelectorActions actions, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Weapon type", {0, 12, 0, -1}, 1, false, identities) {
    if (enabled_groups.size() < 12)
        throw std::invalid_argument("RAGE selector requires twelve weapon groups");
    tab.categories.push_back(&category_); //43487103, before selector construction.
    category_.fixed_height = category_.default_fixed_height = 50;
    category_.resizable = category_.movable = false;
    selector_ = &add<RageWeaponSelector>(category_, 0, u"Weapon type", &selected_group, identities,
                                         enabled_groups, actions);
}
RageWeaponMenu::RageWeaponMenu(Tab& tab, std::uint32_t& selected_group,
                               std::span<BooleanConfig*, 12> enabled_groups,
                               RageWeaponSelectorActions actions, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Weapon type", {0, 12, 0, -1}, 1, false, identities) {
    tab.categories.push_back(&category_); //43487103, before selector construction.
    category_.fixed_height = category_.default_fixed_height = 50;
    category_.resizable = category_.movable = false;
    selector_ = &add<RageWeaponSelector>(category_, 0, u"Weapon type", &selected_group, identities,
                                         enabled_groups, actions);
}
RageWeaponMenu::~RageWeaponMenu() {
    std::erase(tab_.categories, &category_);
}
} // namespace skeet::ui
