#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement player_esp_settings[] = {
    {0x4700A298, ConfigType::hotkey},  {0x45D9D2AC, ConfigType::boolean},
    {0xC410C5D3, ConfigType::boolean}, {0xCFF9EBAD, ConfigType::boolean},
    {0x4D243E0D, ConfigType::color},   {0x49733D3C, ConfigType::boolean},
    {0xD2E84B64, ConfigType::color},   {0xA1C152A1, ConfigType::boolean},
    {0x25FEB6C1, ConfigType::color},   {0x084A8B33, ConfigType::boolean},
    {0xBB3FDF6A, ConfigType::boolean}, {0x6A874B2C, ConfigType::boolean},
    {0x00F64778, ConfigType::color},   {0x43F02F68, ConfigType::boolean},
    {0x8BEC37F4, ConfigType::color},   {0x0F6D81D5, ConfigType::boolean},
    {0xB1D008B7, ConfigType::boolean}, {0xECE872CB, ConfigType::color},
    {0x9C345DFA, ConfigType::boolean}, {0x69C9D9F2, ConfigType::boolean},
    {0x535095F5, ConfigType::boolean}, {0x7451E155, ConfigType::color},
    {0xE9CC8B95, ConfigType::boolean}, {0xE20F6035, ConfigType::color},
    {0xE8D2A207, ConfigType::boolean}, {0x7E3B4C1B, ConfigType::color},
    {0x1DE914E6, ConfigType::boolean}, {0x745EFA1E, ConfigType::color},
    {0x53970838, ConfigType::boolean}, {0x523E4B35, ConfigType::boolean},
    {0x0E995F95, ConfigType::color},   {0x95A660AB, ConfigType::boolean},
    {0x7D18A71F, ConfigType::color},   {0x4564BFC7, ConfigType::integer},
    {0x09CBEDAE, ConfigType::integer},
};
}

PlayerEspMenu::PlayerEspMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Player ESP", {0, 12, 0, 13}, 17, true, identities) {
    require_settings(settings, player_esp_settings);
    storage_.reserve(std::size(player_esp_settings) + 1);
    bindings_.reserve(std::size(player_esp_settings) + 1);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto color = [&](std::uint32_t hash) {
        add<ColorPicker>(category_, hash, value<ColorConfig>(settings, hash), identities);
    };
    add<Label>(category_, 0, u"Activation type", identities);
    add<HotkeyWidget>(category_, 0x4700A298, value<HotkeyConfig>(settings, 0x4700A298), true,
                      identities);
    checkbox(0x45D9D2AC, u"Teammates");
    checkbox(0xC410C5D3, u"Dormant");
    checkbox(0xCFF9EBAD, u"Bounding box");
    color(0x4D243E0D);
    checkbox(0x49733D3C, u"Health bar");
    color(0xD2E84B64);
    checkbox(0xA1C152A1, u"Name");
    color(0x25FEB6C1);
    checkbox(0x084A8B33, u"Flags");
    checkbox(0xBB3FDF6A, u"Weapon text");
    checkbox(0x6A874B2C, u"Weapon icon");
    color(0x00F64778);
    checkbox(0x43F02F68, u"Ammo");
    color(0x8BEC37F4);
    checkbox(0x0F6D81D5, u"Distance");
    checkbox(0xB1D008B7, u"Glow").color = 0xBB82F0F0u;
    color(0xECE872CB);
    checkbox(0x9C345DFA, u"Hit marker");
    checkbox(0x69C9D9F2, u"Hit marker sound");
    checkbox(0x535095F5, u"Visualize aimbot");
    color(0x7451E155);
    checkbox(0xE9CC8B95, u"Visualize aimbot (safe point)");
    color(0xE20F6035);
    checkbox(0xE8D2A207, u"Visualize sounds");
    color(0x7E3B4C1B);
    checkbox(0x1DE914E6, u"Line of sight");
    color(0x745EFA1E);
    checkbox(0x53970838, u"Money");
    checkbox(0x523E4B35, u"Skeleton");
    color(0x0E995F95);
    auto& arrow = checkbox(0x95A660AB, u"Out of FOV arrow");
    color(0x7D18A71F);
    arrow_size_ =
        &add<Slider>(category_, 0x4564BFC7, u"", 1, 30, value<IntegerConfig>(settings, 0x4564BFC7),
                     true, 0x780070u, 1.0f, identities);
    arrow_size_->set_visible(false);
    arrow_distance_ =
        &add<Slider>(category_, 0x09CBEDAE, u"", 1, 100, value<IntegerConfig>(settings, 0x09CBEDAE),
                     true, 0x25u, 1.0f, identities);
    arrow_distance_->set_visible(false);
    arrow.callbacks.push_back({this, arrow_visibility_changed}); // 0x4335BD4D
    tab.categories.push_back(&category_);
}
PlayerEspMenu::~PlayerEspMenu() {
    std::erase(tab_.categories, &category_);
}

void PlayerEspMenu::arrow_visibility_changed(void* context, Widget& source) { // 0x43330832
    auto& self = *static_cast<PlayerEspMenu*>(context);
    auto& checkbox = static_cast<Checkbox&>(source);
    const auto visible = checkbox.value && *checkbox.value != 0;
    self.arrow_size_->set_visible(visible);
    self.arrow_distance_->set_visible(visible);
}

} // namespace skeet::ui
