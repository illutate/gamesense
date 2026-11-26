#include "skeet/menu/anti_aim_menu.hpp"
#include "skeet/anti_aim/config.hpp"
#include "config_binding.hpp"
#include <algorithm>
#include <stdexcept>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement settings_required[] = {
    {0x96C0EF29u, ConfigType::boolean}, {0x6D5F1172u, ConfigType::integer},
    {0x01DE265Eu, ConfigType::integer}, {0x1D43759Bu, ConfigType::integer},
    {0xF2D408F3u, ConfigType::integer}, {0xFE49974Du, ConfigType::integer_array},
    {0x177D7470u, ConfigType::integer}, {0xDCA4B692u, ConfigType::integer},
    {0x4D6D88C6u, ConfigType::integer}, {0x8DFD54BDu, ConfigType::integer_array},
    {0x2716923Fu, ConfigType::boolean}, {0x8C526C4Fu, ConfigType::boolean},
    {0x86C04DC4u, ConfigType::boolean}, {0x697E8050u, ConfigType::hotkey},
    {0xA1DFC38Du, ConfigType::integer},
};
}
AntiAimMenu::AntiAimMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Anti-aimbot angles", {0, 12, 0, 24}, 10, true, identities) {
    require_settings(settings, settings_required);
    if (descriptor(settings, 0xFE49974Du).serialized_size != 24 ||
        descriptor(settings, 0x8DFD54BDu).serialized_size != 16)
        throw std::invalid_argument("Anti-aim requires its six-yaw and four-body offset arrays");
    yaw_offsets_ = value<anti_aim::YawOffsetsConfig>(settings, 0xFE49974Du)->data();
    body_offsets_ = value<anti_aim::BodyYawOffsetsConfig>(settings, 0x8DFD54BDu)->data();
    storage_.reserve(15);
    bindings_.reserve(15);
    tab.categories.push_back(&category_);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto combobox = [&](std::uint32_t hash, std::u16string_view label) -> ComboBox& {
        return add<ComboBox>(category_, hash, label, value<IntegerConfig>(settings, hash), false,
                             identities);
    };
    auto slider = [&](std::uint32_t hash, std::int32_t* setting, int minimum,
                      int maximum) -> Slider& {
        return add<Slider>(category_, hash, u"", minimum, maximum, setting, true, 0xB0u, 1.0f,
                           identities);
    };
    checkbox(0x96C0EF29u, u"Enabled");
    auto& pitch = combobox(0x6D5F1172u, u"Pitch");
    pitch.set_options({{u"Off", 0},
                       {u"Default", 1},
                       {u"Up", 5},
                       {u"Down", 2},
                       {u"Minimal", 3},
                       {u"Random", 4},
                       {u"Custom", 6}});
    pitch_offset_ = &slider(0x01DE265Eu, value<IntegerConfig>(settings, 0x01DE265Eu), -89, 89);
    pitch_offset_->set_visible(false);
    combobox(0x1D43759Bu, u"Yaw base").set_options({{u"Local view", 0}, {u"At targets", 1}});
    yaw_ = &combobox(0xF2D408F3u, u"Yaw");
    yaw_->set_options(
        {{u"Off", 0}, {u"180", 1}, {u"Spin", 3}, {u"Static", 5}, {u"180 Z", 2}, {u"Crosshair", 4}});
    yaw_offset_ = &slider(0xFE49974Du, yaw_offsets_, -180, 180);
    yaw_offset_->set_visible(false);
    jitter_ = &combobox(0x177D7470u, u"Yaw jitter");
    jitter_->set_options(
        {{u"Off", 0}, {u"Offset", 1}, {u"Center", 2}, {u"Random", 3}, {u"Skitter", 4}});
    jitter_->set_visible(false);
    jitter_offset_ = &slider(0xDCA4B692u, value<IntegerConfig>(settings, 0xDCA4B692u), -180, 180);
    jitter_offset_->set_visible(false);
    auto& body = combobox(0x4D6D88C6u, u"Body yaw");
    body.set_options({{u"Off", 0}, {u"Opposite", 1}, {u"Jitter", 2}, {u"Static", 3}});
    body_offset_ = &slider(0x8DFD54BDu, body_offsets_, -180, 180);
    body_offset_->set_visible(false);
    freestanding_body_ = &checkbox(0x2716923Fu, u"Freestanding body yaw");
    freestanding_body_->set_visible(false);
    checkbox(0x8C526C4Fu, u"Edge yaw");
    checkbox(0x86C04DC4u, u"Freestanding");
    add<HotkeyWidget>(category_, 0x697E8050u, value<HotkeyConfig>(settings, 0x697E8050u), true,
                      identities);
    add<Slider>(category_, 0xA1DFC38Du, u"Roll", -45, 45,
                value<IntegerConfig>(settings, 0xA1DFC38Du), true, 0xB0u, 1.0f, identities);
    pitch.callbacks.push_back({this, pitch_changed}); // 0x433CF43F
    yaw_->callbacks.push_back({this, yaw_changed}); // 0x433CC833
    jitter_->callbacks.push_back({this, jitter_changed}); // 0x433598F4
    body.callbacks.push_back({this, body_changed}); // 0x433C65CC
}
AntiAimMenu::~AntiAimMenu() {
    std::erase(tab_.categories, &category_);
}
anti_aim::MenuDependencies AntiAimMenu::dependencies() {
    return {*pitch_offset_,
            *yaw_offset_,
            *jitter_,
            *jitter_offset_,
            *body_offset_,
            *freestanding_body_,
            std::span<std::int32_t, 6>(yaw_offsets_, 6),
            std::span<std::int32_t, 4>(body_offsets_, 4)};
}
void AntiAimMenu::pitch_changed(void* context, Widget& source) {
    auto bindings = static_cast<AntiAimMenu*>(context)->dependencies();
    anti_aim::pitch_changed(bindings, static_cast<ComboBox&>(source));
}
void AntiAimMenu::yaw_changed(void* context, Widget& source) {
    auto bindings = static_cast<AntiAimMenu*>(context)->dependencies();
    anti_aim::yaw_changed(bindings, static_cast<ComboBox&>(source));
}
void AntiAimMenu::jitter_changed(void* context, Widget& source) {
    auto& self = *static_cast<AntiAimMenu*>(context);
    auto bindings = self.dependencies();
    anti_aim::jitter_changed(bindings, static_cast<ComboBox&>(source), *self.yaw_);
}
void AntiAimMenu::body_changed(void* context, Widget& source) {
    auto bindings = static_cast<AntiAimMenu*>(context)->dependencies();
    anti_aim::body_yaw_changed(bindings, static_cast<ComboBox&>(source));
}
} // namespace skeet::ui
