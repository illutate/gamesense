#include "skeet/menu/anti_aim_extra_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
FakeLagMenu::FakeLagMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Fake lag", {12, 12, 0, 12}, 13, true, identities) {
    constexpr Requirement required[] = {{0xF9BBEC7Bu, ConfigType::boolean},
                                        {0x27D21BD4u, ConfigType::hotkey},
                                        {0x2A71D5FCu, ConfigType::integer},
                                        {0xCA9768E6u, ConfigType::integer},
                                        {0xF17FEF11u, ConfigType::integer}};
    require_settings(settings, required);
    tab.categories.push_back(&category_);
    add<Checkbox>(category_, 0xF9BBEC7Bu, u"Enabled", value<BooleanConfig>(settings, 0xF9BBEC7Bu),
                  identities);
    add<HotkeyWidget>(category_, 0x27D21BD4u, value<HotkeyConfig>(settings, 0x27D21BD4u), true,
                      identities);
    auto& amount = add<ComboBox>(category_, 0x2A71D5FCu, u"Amount",
                                 value<IntegerConfig>(settings, 0x2A71D5FCu), false, identities);
    amount.set_options({{u"Dynamic", 0}, {u"Maximum", 2}, {u"Fluctuate", 3}});
    add<Slider>(category_, 0xCA9768E6u, u"Variance", 0, 100,
                value<IntegerConfig>(settings, 0xCA9768E6u), true, 0x25u, 1.0f, identities);
    limit_ = &add<Slider>(category_, 0xF17FEF11u, u"Limit", 1, 15,
                          value<IntegerConfig>(settings, 0xF17FEF11u), true, 0u, 1.0f, identities);
}
FakeLagMenu::~FakeLagMenu() {
    std::erase(tab_.categories, &category_);
}
} // namespace skeet::ui
