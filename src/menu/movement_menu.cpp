#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement movement_settings[] = {
    {0x4CD278CA, ConfigType::boolean}, {0xD0CA5B87, ConfigType::boolean},
    {0xDB2172A1, ConfigType::boolean}, {0xFE3FDAAB, ConfigType::boolean},
    {0xF2EB89E9, ConfigType::boolean}, {0x6F6ACC9B, ConfigType::integer},
    {0xC94C5F0B, ConfigType::integer}, {0x3095C574, ConfigType::boolean},
    {0x3FDF2566, ConfigType::boolean}, {0x2B499E77, ConfigType::hotkey},
    {0x3027DC11, ConfigType::integer}, {0x3127DE82, ConfigType::integer},
    {0xCA7FC107, ConfigType::boolean}, {0x712C1936, ConfigType::hotkey},
    {0x8E38F1F3, ConfigType::boolean}, {0x6E4E5CC7, ConfigType::color},
    {0x5B1A5C7F, ConfigType::integer}, {0xB2CDBB7D, ConfigType::boolean},
    {0xE2FFCCF0, ConfigType::hotkey},  {0x8C13C109, ConfigType::boolean},
    {0x5BBB224E, ConfigType::hotkey},  {0xB5E64AD1, ConfigType::boolean},
};
}

MovementMenu::MovementMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Movement", {12, 12, 0, 12}, 13, true, identities) {
    require_settings(settings, movement_settings);
    storage_.reserve(std::size(movement_settings));
    bindings_.reserve(std::size(movement_settings));
    tab.categories.push_back(&category_);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto hotkey = [&](std::uint32_t hash) -> HotkeyWidget& {
        return add<HotkeyWidget>(category_, hash, value<HotkeyConfig>(settings, hash), true,
                                 identities);
    };
    checkbox(0x4CD278CA, u"Standalone quick stop");
    checkbox(0xD0CA5B87, u"Infinite duck").color = 0xBB82F0F0;
    checkbox(0xDB2172A1, u"Easy strafe");
    checkbox(0xFE3FDAAB, u"Bunny hop");
    auto& air_strafe = checkbox(0xF2EB89E9, u"Air strafe");
    direction_ = &add<MultiSelect>(category_, 0x6F6ACC9B, u"Air strafe direction",
                                   value<IntegerConfig>(settings, 0x6F6ACC9B), identities);
    direction_->add_option("View angles", 1);
    direction_->add_option("Movement keys", 2);
    direction_->set_visible(false);
    smoothing_ =
        &add<Slider>(category_, 0xC94C5F0B, u"Air strafe smoothing", 0, 100,
                     value<IntegerConfig>(settings, 0xC94C5F0B), true, 0x25u, 1.0f, identities);
    smoothing_->set_visible(false);
    avoid_collisions_ = &checkbox(0x3095C574, u"Avoid collisions");
    avoid_collisions_->set_visible(false);
    z_hop_ = &checkbox(0x3FDF2566, u"Z-Hop");
    z_hop_->set_visible(false);
    z_hop_key_ = &hotkey(0x2B499E77);
    z_hop_key_->set_visible(false);
    z_hop_first_ =
        &add<Slider>(category_, 0x3027DC11, u"", 100, 300,
                     value<IntegerConfig>(settings, 0x3027DC11), true, 0u, 0.1f, identities);
    z_hop_first_->set_visible(false);
    z_hop_second_ =
        &add<Slider>(category_, 0x3127DE82, u"", 100, 500,
                     value<IntegerConfig>(settings, 0x3127DE82), true, 0u, 0.1f, identities);
    z_hop_second_->set_visible(false);
    pre_speed_ = &checkbox(0xCA7FC107, u"Pre-speed");
    pre_speed_->set_visible(false);
    pre_speed_key_ = &hotkey(0x712C1936);
    pre_speed_key_->set_visible(false);
    checkbox(0x8E38F1F3, u"No fall damage");
    add<ColorPicker>(category_, 0x6E4E5CC7, value<ColorConfig>(settings, 0x6E4E5CC7), identities);
    auto& air_duck = add<ComboBox>(category_, 0x5B1A5C7F, u"Air duck",
                                   value<IntegerConfig>(settings, 0x5B1A5C7F), false, identities);
    air_duck.set_options({{u"Off", 0}, {u"On", 1}, {u"Spam", 3}, {u"Fake", 2}, {u"On land", 4}});
    checkbox(0xB2CDBB7D, u"Blockbot");
    hotkey(0xE2FFCCF0);
    checkbox(0x8C13C109, u"Jump at edge");
    hotkey(0x5BBB224E);
    checkbox(0xB5E64AD1, u"Fast walk");
    air_strafe.callbacks.push_back({this, air_strafe_changed}); // 0x4336E86C
    z_hop_->callbacks.push_back({this, z_hop_changed}); // 0x433AFC9F
}
MovementMenu::~MovementMenu() {
    std::erase(tab_.categories, &category_);
}

void MovementMenu::update_z_hop(const Checkbox& source) { // 0x43337663
    const auto visible = source.visible && source.value && *source.value != 0;
    z_hop_first_->set_visible(visible);
    z_hop_second_->set_visible(visible);
}
void MovementMenu::z_hop_changed(void* context, Widget& source) { // 0x433AFC9F
    static_cast<MovementMenu*>(context)->update_z_hop(static_cast<Checkbox&>(source));
}
void MovementMenu::air_strafe_changed(void* context, Widget& source) { // 0x433D1146
    auto& self = *static_cast<MovementMenu*>(context);
    auto& checkbox = static_cast<Checkbox&>(source);
    const auto visible = checkbox.value && *checkbox.value != 0;
    self.direction_->set_visible(visible);
    self.smoothing_->set_visible(visible);
    self.z_hop_->set_visible(visible);
    self.z_hop_key_->set_visible(visible);
    self.pre_speed_->set_visible(visible);
    self.pre_speed_key_->set_visible(visible);
    self.update_z_hop(*self.z_hop_);
    self.avoid_collisions_->set_visible(visible);
}

} // namespace skeet::ui
