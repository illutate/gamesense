#include "skeet/menu/anti_aim_extra_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
AntiAimOtherMenu::AntiAimOtherMenu(Tab& tab, ConfigRegistries& settings,
                                   WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Other", {12, 12, 12, 12}, 13, true, identities) {
    constexpr Requirement required[] = {
        {0x4DF394FFu, ConfigType::boolean}, {0x7293DE2Au, ConfigType::hotkey},
        {0x8F8A4366u, ConfigType::integer}, {0xB2467427u, ConfigType::boolean},
        {0x8F4F3C91u, ConfigType::hotkey},  {0x8494FC0Du, ConfigType::boolean},
        {0xAE772257u, ConfigType::hotkey}};
    require_settings(settings, required);
    tab.categories.push_back(&category_);
    add<Checkbox>(category_, 0x4DF394FFu, u"Slow motion",
                  value<BooleanConfig>(settings, 0x4DF394FFu), identities);
    add<HotkeyWidget>(category_, 0x7293DE2Au, value<HotkeyConfig>(settings, 0x7293DE2Au), true,
                      identities);
    auto& legs = add<ComboBox>(category_, 0x8F8A4366u, u"Leg movement",
                               value<IntegerConfig>(settings, 0x8F8A4366u), false, identities);
    legs.set_options({{u"Off", 0}, {u"Always slide", 1}, {u"Never slide", 2}});
    auto& on_shot = add<Checkbox>(category_, 0xB2467427u, u"On shot anti-aim",
                                  value<BooleanConfig>(settings, 0xB2467427u), identities);
    on_shot.color = 0xBB82F0F0u;
    add<HotkeyWidget>(category_, 0x8F4F3C91u, value<HotkeyConfig>(settings, 0x8F4F3C91u), true,
                      identities);
    auto& fake = add<Checkbox>(category_, 0x8494FC0Du, u"Fake peek",
                               value<BooleanConfig>(settings, 0x8494FC0Du), identities);
    fake.color = 0xBB82F0F0u;
    add<HotkeyWidget>(category_, 0xAE772257u, value<HotkeyConfig>(settings, 0xAE772257u), true,
                      identities);
}
AntiAimOtherMenu::~AntiAimOtherMenu() {
    std::erase(tab_.categories, &category_);
}
} // namespace skeet::ui
