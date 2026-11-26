#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement other_esp_settings[] = {
    {0xD7CD1B0B, ConfigType::boolean}, {0x25B4B670, ConfigType::boolean},
    {0xEDF6C582, ConfigType::boolean}, {0x78A1B150, ConfigType::boolean},
    {0xAD37A2EB, ConfigType::boolean}, {0xF2937E07, ConfigType::boolean},
    {0xDC4C1BB9, ConfigType::boolean}, {0x85BDA341, ConfigType::boolean},
    {0x38623779, ConfigType::boolean}, {0x1E7432A5, ConfigType::boolean},
    {0x559F78DC, ConfigType::boolean}, {0x0E533025, ConfigType::boolean},
    {0x6834D5DA, ConfigType::boolean}, {0xCBE162F8, ConfigType::boolean},
    {0x553D3C3F, ConfigType::boolean}, {0x89653491, ConfigType::boolean},
    {0x75A591C2, ConfigType::boolean}, {0x325AF11E, ConfigType::boolean},
    {0x6E6CDAD7, ConfigType::boolean}, {0x5DA3152C, ConfigType::boolean},
    {0xF2A389D2, ConfigType::boolean}, {0x3B6A149E, ConfigType::boolean},
    {0xA3B423BE, ConfigType::boolean}, {0x52DAAA5D, ConfigType::boolean},
    {0x081D65D4, ConfigType::boolean}, {0xD13FFDF6, ConfigType::boolean},
    {0x6A586832, ConfigType::boolean}, {0xF8774353, ConfigType::boolean},
    {0xFFD5C89D, ConfigType::boolean}, {0x2802A1F6, ConfigType::boolean},
    {0x2B4CDE08, ConfigType::boolean}, {0x4FD3F678, ConfigType::boolean},
    {0x42694ACE, ConfigType::boolean}, {0x2D915386, ConfigType::boolean},
    {0xA10DC407, ConfigType::color},   {0xB8C2C5BC, ConfigType::color},
    {0x2B61D25A, ConfigType::color},   {0x2ADA481B, ConfigType::color},
    {0x8428F8F9, ConfigType::color},   {0xC2611C1F, ConfigType::color},
    {0xA6B878A8, ConfigType::color},   {0xDB9A269A, ConfigType::color},
    {0x174ABFD6, ConfigType::color},   {0xF407872B, ConfigType::color},
    {0x082CE178, ConfigType::color},   {0x928C112A, ConfigType::color},
    {0xCEE5C756, ConfigType::color},   {0x55436936, ConfigType::color},
    {0x72894F7D, ConfigType::color},   {0xCF605580, ConfigType::color},
    {0x8B5C32AE, ConfigType::color},   {0x76DA764A, ConfigType::color},
    {0x406A4767, ConfigType::color},   {0xC11B6FBD, ConfigType::color},
    {0xB424B6AE, ConfigType::color},   {0xC8555DD4, ConfigType::color},
    {0x58386184, ConfigType::color},   {0x06D6A3C6, ConfigType::color},
    {0xE5F28DFE, ConfigType::color},   {0x6D3D6833, ConfigType::integer},
    {0x59A226C2, ConfigType::integer}, {0x7174D684, ConfigType::hotkey},
    {0x85BDA342, ConfigType::integer}, {0x85BDA343, ConfigType::boolean},
    {0x85BDA344, ConfigType::color},   {0x85BDA345, ConfigType::color},
    {0x85BDA346, ConfigType::boolean}, {0x85BDA347, ConfigType::color},
    {0xAD37A2EC, ConfigType::boolean}, {0xAD37A2ED, ConfigType::color},
    {0xAD37A2EE, ConfigType::integer}, {0xAD37A2EF, ConfigType::integer},
    {0xAD37A2F0, ConfigType::integer}, {0x89653492, ConfigType::boolean},
    {0x89653493, ConfigType::integer}, {0x89653494, ConfigType::integer},
    {0x89653495, ConfigType::integer}, {0x89653496, ConfigType::integer},
};
struct DangerZoneControl {
    std::uint32_t enabled, color;
    std::u16string_view label;
};
constexpr DangerZoneControl danger_zone_controls[] = {
    {0x75A591C2, 0xDB9A269A, u"Drone gun"},
    {0x325AF11E, 0x174ABFD6, u"Blackhawk"},
    {0x6E6CDAD7, 0xF407872B, u"Drone"},
    {0x5DA3152C, 0x082CE178, u"Random case"},
    {0xF2A389D2, 0x928C112A, u"Tool case"},
    {0x3B6A149E, 0xCEE5C756, u"Pistol case"},
    {0xA3B423BE, 0x55436936, u"Explosive case"},
    {0x52DAAA5D, 0x72894F7D, u"Heavy weapon case"},
    {0x081D65D4, 0xCF605580, u"Light weapon case"},
    {0xD13FFDF6, 0x8B5C32AE, u"Dufflebag"},
    {0x6A586832, 0x76DA764A, u"Jammer"},
    {0xF8774353, 0x406A4767, u"Ammobox"},
    {0xFFD5C89D, 0xC11B6FBD, u"Armor"},
    {0x2802A1F6, 0xB424B6AE, u"Parachute pack"},
    {0x2B4CDE08, 0xC8555DD4, u"Briefcase"},
    {0x4FD3F678, 0x58386184, u"Tablet upgrade zone"},
    {0x42694ACE, 0x06D6A3C6, u"Tablet upgrade drone"},
    {0x2D915386, 0xE5F28DFE, u"Cash stack"},
};
}

OtherEspMenu::OtherEspMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Other ESP", {12, 12, 0, 12}, 9, true, identities) {
    require_settings(settings, other_esp_settings);
    storage_.reserve(std::size(other_esp_settings) + 4);
    bindings_.reserve(std::size(other_esp_settings) + 4);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto color = [&](std::uint32_t hash) -> ColorPicker& {
        return add<ColorPicker>(category_, hash, value<ColorConfig>(settings, hash), identities);
    };
    auto tie_pair = [&](Checkbox& owner, std::size_t offset, Widget& first, Widget& second) {
        first.set_visible(false);
        second.set_visible(false);
        dependencies_[offset] = {&first, &dependencies_[offset + 1]};
        dependencies_[offset + 1] = {&second, &owner.tied};
        owner.tied.next = &dependencies_[offset];
    };
    checkbox(0xD7CD1B0B, u"Radar");
    auto& dropped = add<MultiSelect>(category_, 0x6D3D6833, u"Dropped weapons",
                                     value<IntegerConfig>(settings, 0x6D3D6833), identities, false);
    dropped.add_option("Icon", 1);
    dropped.add_option("Text", 2);
    dropped.add_option("Glow", 4);
    dropped.add_option("Ammo", 8);
    dropped.add_option("Distance", 16);
    color(0xA10DC407);
    checkbox(0x25B4B670, u"Grenades");
    color(0xB8C2C5BC);
    checkbox(0xEDF6C582, u"Inaccuracy overlay");
    color(0x2B61D25A);
    checkbox(0x78A1B150, u"Recoil overlay");
    checkbox(0xAD37A2EB, u"Crosshair");
    auto& custom_scope = checkbox(0xAD37A2EC, u"Custom scope lines");
    color(0xAD37A2ED);
    auto& custom_scope_pos =
        add<Slider>(category_, 0xAD37A2EE, u"Initial position", 0, 500,
                    value<IntegerConfig>(settings, 0xAD37A2EE), true, 0, 1.0f, identities);
    auto& custom_scope_off =
        add<Slider>(category_, 0xAD37A2EF, u"Offset", 0, 500,
                    value<IntegerConfig>(settings, 0xAD37A2EF), true, 0, 1.0f, identities);
    auto& custom_scope_spd =
        add<Slider>(category_, 0xAD37A2F0, u"Fade animation speed", 3, 20,
                    value<IntegerConfig>(settings, 0xAD37A2F0), true, 0x00720066u, 1.0f,
                    identities);
    custom_scope_spd.value_labels.emplace(3, L"Off");
    custom_scope_pos.set_visible(false);
    custom_scope_off.set_visible(false);
    custom_scope_spd.set_visible(false);
    dependencies_[12] = {&custom_scope_pos, &dependencies_[13]};
    dependencies_[13] = {&custom_scope_off, &dependencies_[14]};
    dependencies_[14] = {&custom_scope_spd, &custom_scope.tied};
    custom_scope.tied.next = &dependencies_[12];
    custom_scope.config_loaded();
    checkbox(0xF2937E07, u"Bomb");
    color(0x2ADA481B);
    auto& trajectory = checkbox(0xDC4C1BB9, u"Grenade trajectory");
    trajectory.color = 0xBB82F0F0u;
    color(0x8428F8F9);
    auto& hit_label = add<Label>(category_, 0, u"Grenade trajectory (hit)", identities);
    auto& hit_color = color(0xC2611C1F);
    tie_pair(trajectory, 0, hit_label, hit_color);
    auto& proximity = checkbox(0x85BDA341, u"Grenade proximity warning");
    proximity.color = 0xBB82F0F0u;
    auto& proximity_style = add<ComboBox>(category_, 0x85BDA342, u"Grenade warning style",
                                          value<IntegerConfig>(settings, 0x85BDA342), false,
                                          identities);
    proximity_style.set_options({{u"Neverlose", 1},
                                 {u"CS2", 2},
                                 {u"Lunar", 3},
                                 {u"Legacy", 4},
                                 {u"Default", 0}});
    auto& molotov_area = checkbox(0x85BDA343, u"Molotov area");
    auto& molotov_harm_label = add<Label>(category_, 0, u"Molotov area (harmful)", identities);
    auto& molotov_harm_color = color(0x85BDA344);
    auto& molotov_safe_label = add<Label>(category_, 0, u"Molotov area (safe)", identities);
    auto& molotov_safe_color = color(0x85BDA345);
    auto& smoke_area = checkbox(0x85BDA346, u"Smoke area");
    auto& smoke_area_color = color(0x85BDA347);
    proximity_style.set_visible(false);
    molotov_area.set_visible(false);
    molotov_harm_label.set_visible(false);
    molotov_harm_color.set_visible(false);
    molotov_safe_label.set_visible(false);
    molotov_safe_color.set_visible(false);
    smoke_area.set_visible(false);
    smoke_area_color.set_visible(false);
    dependencies_[4] = {&proximity_style, &dependencies_[5]};
    dependencies_[5] = {&molotov_area, &dependencies_[6]};
    dependencies_[6] = {&molotov_harm_label, &dependencies_[7]};
    dependencies_[7] = {&molotov_harm_color, &dependencies_[8]};
    dependencies_[8] = {&molotov_safe_label, &dependencies_[9]};
    dependencies_[9] = {&molotov_safe_color, &dependencies_[10]};
    dependencies_[10] = {&smoke_area, &dependencies_[11]};
    dependencies_[11] = {&smoke_area_color, &proximity.tied};
    proximity.tied.next = &dependencies_[4];
    proximity.config_loaded();
    checkbox(0x38623779, u"Spectators");
    checkbox(0x1E7432A5, u"Penetration reticle").color = 0xBB82F0F0u;
    checkbox(0x559F78DC, u"Hostages");
    color(0xA6B878A8);
    auto& indicators =
        add<MultiSelect>(category_, 0x59A226C2, u"Feature indicators",
                         value<IntegerConfig>(settings, 0x59A226C2), identities, false);
    indicators.add_option("Force safe point", 1);
    indicators.add_option("Force body aim", 2);
    indicators.add_option("Ping spike", 4);
    indicators.add_option("Double tap", 8);
    indicators.add_option("Duck peek assist", 16);
    indicators.add_option("Freestanding", 32);
    indicators.add_option("On shot anti-aim", 64);
    indicators.add_option("Minimum damage override", 256);
    auto& shared = checkbox(0x0E533025, u"Shared ESP");
    add<HotkeyWidget>(category_, 0x7174D684, value<HotkeyConfig>(settings, 0x7174D684), true,
                      identities);
    auto& other_team = checkbox(0x6834D5DA, u"Shared ESP with other team");
    auto& restrict_updates = checkbox(0xCBE162F8, u"Restrict shared ESP updates");
    tie_pair(shared, 2, other_team, restrict_updates);
    checkbox(0x553D3C3F, u"Upgrade tablet");
    auto& danger_zone = checkbox(0x89653491, u"Danger Zone items");
    for (const auto& control : danger_zone_controls) {
        checkbox(control.enabled, control.label);
        last_danger_zone_control_ = &color(control.color);
    }
    danger_zone.callbacks.push_back({this, danger_zone_changed}); // 0x433C1019
    danger_zone_changed(this, danger_zone); // 0x4348ABA1..0x4348ABAD
    auto& viewmodel = checkbox(0x89653492, u"Viewmodel changer");
    auto& vm_fov = add<Slider>(category_, 0x89653493, u"Offset fov", -1800, 1800,
                               value<IntegerConfig>(settings, 0x89653493), true, 0, 0.1f,
                               identities);
    auto& vm_x = add<Slider>(category_, 0x89653494, u"Offset x", -1800, 1800,
                             value<IntegerConfig>(settings, 0x89653494), true, 0, 0.1f,
                             identities);
    auto& vm_y = add<Slider>(category_, 0x89653495, u"Offset y", -1800, 1800,
                             value<IntegerConfig>(settings, 0x89653495), true, 0, 0.1f,
                             identities);
    auto& vm_z = add<Slider>(category_, 0x89653496, u"Offset z", -1800, 1800,
                             value<IntegerConfig>(settings, 0x89653496), true, 0, 0.1f,
                             identities);
    vm_fov.set_visible(false);
    vm_x.set_visible(false);
    vm_y.set_visible(false);
    vm_z.set_visible(false);
    dependencies_[15] = {&vm_fov, &dependencies_[16]};
    dependencies_[16] = {&vm_x, &dependencies_[17]};
    dependencies_[17] = {&vm_y, &dependencies_[18]};
    dependencies_[18] = {&vm_z, &viewmodel.tied};
    viewmodel.tied.next = &dependencies_[15];
    viewmodel.config_loaded();
    tab.categories.push_back(&category_);
}
OtherEspMenu::~OtherEspMenu() {
    std::erase(tab_.categories, &category_);
}

void OtherEspMenu::danger_zone_changed(void* context, Widget& source) { // 0x4334D372
    auto& self = *static_cast<OtherEspMenu*>(context);
    auto& checkbox = static_cast<Checkbox&>(source);
    const auto visible = checkbox.value && *checkbox.value != 0;
    auto position =
        std::find(self.category_.elements.begin(), self.category_.elements.end(), &source);
    if (position == self.category_.elements.end())
        return;
    for (++position; position != self.category_.elements.end(); ++position) {
        (*position)->set_visible(visible);
        if (*position == self.last_danger_zone_control_)
            break;
    }
}

} // namespace skeet::ui
