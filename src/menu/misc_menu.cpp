#include "skeet/menu/misc_menu.hpp"
#include "skeet/misc/runtime.hpp"
#include "skeet/misc/rebuy_fix.hpp"
#include "skeet/misc/quick_switch.hpp"
#include "skeet/misc/reveal_ranks.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement required[] = {
    {0xFC225A4F, ConfigType::integer}, {0xCA80F9ED, ConfigType::integer},
    {0x7A5909EE, ConfigType::boolean}, {0xA7C81D9B, ConfigType::integer},
    {0x2E314CE2, ConfigType::boolean}, {0x3D5DE026, ConfigType::boolean},
    {0x3952AAA6, ConfigType::integer}, {0x0D634908, ConfigType::boolean},
    {0x30E0CFE7, ConfigType::boolean}, {0xD351D519, ConfigType::boolean},
    {0x141F80B7, ConfigType::boolean}, {0x9F6C4676, ConfigType::boolean},
    {0x5086BCF5, ConfigType::boolean}, {0x0CE14F5A, ConfigType::boolean},
    {0x969EEF1F, ConfigType::boolean}, {0xDB791DB8, ConfigType::hotkey},
    {0x510F682D, ConfigType::integer}, {0x37A61386, ConfigType::boolean},
    {0x916F47DA, ConfigType::boolean}, {0x3C4135FF, ConfigType::hotkey},
    {0xC5290E81, ConfigType::integer}, {0x77D31850, ConfigType::hotkey},
    {0x245CEDB5, ConfigType::boolean}, {0x473CC2DE, ConfigType::hotkey},
    {0x177AB88C, ConfigType::boolean}, {0x6ED248E1, ConfigType::boolean},
    {0x52EADF91, ConfigType::boolean}, {0xA81D4E21, ConfigType::boolean},
    {0xA81D4E22, ConfigType::integer}, {0xA81D4E23, ConfigType::integer},
    {0xA81D4E24, ConfigType::integer},
};
}

MiscellaneousMenu::MiscellaneousMenu(Tab& tab, ConfigRegistries& settings,
                                     WidgetIdentities& identities, misc::Runtime& runtime,
                                     std::uint8_t& upgrade_tablet, bool expose_tablet)
    : tab_(tab), category_(&tab, u"Miscellaneous", {0, 12, 0, 24}, 13, true, identities),
      runtime_(runtime) {
    require_settings(settings, required);
    runtime_.auto_accept_enabled = value<BooleanConfig>(settings, 0x141F80B7);
    runtime_.upgrade_tablet_enabled = &upgrade_tablet;
    runtime_.clan_tag.enabled = value<BooleanConfig>(settings, 0x9F6C4676);
    runtime_.ping_spike.enabled = value<BooleanConfig>(settings, 0x916F47DA);
    runtime_.ping_spike.hotkey = value<HotkeyConfig>(settings, 0x3C4135FF);
    runtime_.ping_spike.milliseconds = value<IntegerConfig>(settings, 0xC5290E81);
    if (runtime_.rebuy_fix)
        runtime_.rebuy_fix->enabled = value<BooleanConfig>(settings, 0x6ED248E1);
    if (runtime_.quick_switch)
        runtime_.quick_switch->enabled = value<BooleanConfig>(settings, 0x0D634908);
    if (runtime_.reveal_ranks)
        runtime_.reveal_ranks->enabled = value<BooleanConfig>(settings, 0x30E0CFE7);
    storage_.reserve(40);
    bindings_.reserve(40);
    tab.categories.push_back(&category_);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view name) -> Checkbox& {
        return add<Checkbox>(category_, hash, name, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto slider = [&](std::uint32_t hash, std::u16string_view name, int minimum, int maximum,
                      std::uint32_t suffix) -> Slider& {
        return add<Slider>(category_, hash, name, minimum, maximum,
                           value<IntegerConfig>(settings, hash), true, suffix, 1.0f, identities);
    };
    auto key = [&](std::uint32_t hash) -> HotkeyWidget& {
        return add<HotkeyWidget>(category_, hash, value<HotkeyConfig>(settings, hash), true,
                                 identities);
    };
    auto tie = [&](std::size_t index, Checkbox& source, Widget& target) {
        target.set_visible(false);
        dependencies_[index] = {&target, &source.tied};
        source.tied.next = &dependencies_[index];
    };
    slider(0xFC225A4F, u"Override FOV", 1, 135, 0xB0);
    slider(0xCA80F9ED, u"Override zoom FOV", 0, 100, 0x25);
    auto& knife = checkbox(0x7A5909EE, u"Knifebot");
    auto& knife_mode = add<MultiSelect>(category_, 0xA7C81D9B, u"",
                                        value<IntegerConfig>(settings, 0xA7C81D9B), identities);
    knife_mode.add_option("Swing", 1);
    knife_mode.add_option("Full stab", 2);
    tie(0, knife, knife_mode);
    checkbox(0x2E314CE2, u"Zeusbot");
    auto& automatic = checkbox(0x3D5DE026, u"Automatic weapons");
    tie(1, automatic, slider(0x3952AAA6, u"", 0, 200, 0x0073006D));
    checkbox(0x0D634908, u"Quick switch");
    checkbox(0x30E0CFE7, u"Reveal competitive ranks");
    checkbox(0xD351D519, u"Reveal Overwatch players");
    checkbox(0x141F80B7, u"Auto-accept matchmaking")
        .callbacks.push_back({this, auto_accept_changed});
    checkbox(0x9F6C4676, u"Clan tag spammer");
    checkbox(0x5086BCF5, u"Log weapon purchases");
    checkbox(0x0CE14F5A, u"Log damage dealt");
    auto& grenade = checkbox(0x969EEF1F, u"Automatic grenade release");
    key(0xDB791DB8);
    tie(2, grenade, slider(0x510F682D, u"Minimum grenade damage", 1, 50, 0));
    checkbox(0x37A61386, u"Super toss");
    auto& ping = checkbox(0x916F47DA, u"Ping spike");
    ping.callbacks.push_back({this, ping_spike_changed});
    key(0x3C4135FF);
    tie(3, ping, slider(0xC5290E81, u"", 1, 200, 0x0073006D));
    add<Label>(category_, 0, u"Free look", identities);
    key(0x77D31850);
    checkbox(0x245CEDB5, u"Persistent kill feed");
    add<Label>(category_, 0, u"Last second defuse", identities);
    key(0x473CC2DE);
    checkbox(0x177AB88C, u"Disable sv_pure");
    if (expose_tablet)
        add<Checkbox>(category_, 0, u"Upgrade tablet", &upgrade_tablet, identities)
            .callbacks.push_back({this, upgrade_tablet_changed});
    checkbox(0x6ED248E1, u"Rebuy fix");
    checkbox(0x52EADF91, u"Draw console output");
    add<Button>(category_, 0, u"Steal player name", Point{158, 28},
                WidgetCallback{this, steal_player_name}, identities);
    add<Button>(category_, 0, u"Dump MM wins", Point{158, 28},
                WidgetCallback{this, dump_matchmaking_wins}, identities);
    auto& auto_buy = checkbox(0xA81D4E21, u"Auto-Buy");
    auto& buy_primary = add<ComboBox>(category_, 0xA81D4E22, u"Auto-Buy: Primary",
                                      value<IntegerConfig>(settings, 0xA81D4E22), false,
                                      identities);
    buy_primary.set_options(
        {{u"-", 0}, {u"AWP", 1}, {u"Auto-Sniper", 2}, {u"Scout", 3}, {u"Negev", 4}});
    auto& buy_secondary = add<ComboBox>(category_, 0xA81D4E23, u"Auto-Buy: Secondary",
                                        value<IntegerConfig>(settings, 0xA81D4E23), false,
                                        identities);
    buy_secondary.set_options({{u"-", 0},
                               {u"R8 Revolver / Deagle", 1},
                               {u"Dual Berettas", 2},
                               {u"FN57 / Tec9 / CZ75-Auto", 3}});
    auto& buy_gear = add<MultiSelect>(category_, 0xA81D4E24, u"Auto-Buy: Gear",
                                      value<IntegerConfig>(settings, 0xA81D4E24), identities,
                                      false);
    buy_gear.add_option("Kevlar", 1);
    buy_gear.add_option("Helmet", 2);
    buy_gear.add_option("Defuse Kit", 4);
    buy_gear.add_option("Grenade", 8);
    buy_gear.add_option("Molotov", 16);
    buy_gear.add_option("Smoke", 32);
    buy_gear.add_option("Taser", 64);
    buy_primary.set_visible(false);
    buy_secondary.set_visible(false);
    buy_gear.set_visible(false);
    dependencies_[4] = {&buy_primary, &dependencies_[5]};
    dependencies_[5] = {&buy_secondary, &dependencies_[6]};
    dependencies_[6] = {&buy_gear, &auto_buy.tied};
    auto_buy.tied.next = &dependencies_[4];
}
MiscellaneousMenu::~MiscellaneousMenu() {
    std::erase(tab_.categories, &category_);
}
void MiscellaneousMenu::auto_accept_changed(void* context, Widget&) {
    static_cast<MiscellaneousMenu*>(context)->runtime_.auto_accept_changed();
}
void MiscellaneousMenu::ping_spike_changed(void* context, Widget& widget) {
    const auto* setting = static_cast<Checkbox&>(widget).value;
    static_cast<MiscellaneousMenu*>(context)->runtime_.ping_spike_changed(setting && *setting != 0);
}
void MiscellaneousMenu::upgrade_tablet_changed(void* context, Widget&) {
    static_cast<MiscellaneousMenu*>(context)->runtime_.upgrade_tablet_changed();
}
void MiscellaneousMenu::steal_player_name(void* context, Widget&) {
    static_cast<MiscellaneousMenu*>(context)->runtime_.steal_player_name();
}
void MiscellaneousMenu::dump_matchmaking_wins(void* context, Widget&) {
    static_cast<MiscellaneousMenu*>(context)->runtime_.dump_matchmaking_wins();
}
} // namespace skeet::ui
