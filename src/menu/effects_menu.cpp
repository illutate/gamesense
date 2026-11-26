#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>
#include <stdexcept>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement effects_settings[] = {
    {0x461D3721, ConfigType::boolean}, {0x623BD4F2, ConfigType::boolean},
    {0x3359E29C, ConfigType::boolean}, {0xFBD4768D, ConfigType::boolean},
    {0x171EED00, ConfigType::boolean}, {0xEAFCEAD9, ConfigType::boolean},
    {0xA8321370, ConfigType::boolean}, {0xD35F8540, ConfigType::boolean},
    {0x264E3E21, ConfigType::boolean}, {0x4CBB0E86, ConfigType::boolean},
    {0xB589F662, ConfigType::boolean}, {0x3A5285A6, ConfigType::boolean},
    {0xD54A1CBD, ConfigType::boolean}, {0xC82CBD3B, ConfigType::boolean},
    {0x2A0B0DF3, ConfigType::color},   {0x635F47DE, ConfigType::color},
    {0xF9200DEF, ConfigType::color},   {0x70BA27E0, ConfigType::integer},
    {0x9B4090D9, ConfigType::integer}, {0x8BED781E, ConfigType::integer},
    {0x2E5DD918, ConfigType::integer}, {0x4C044F71, ConfigType::integer},
    {0x921CAA0A, ConfigType::integer}, {0xA8F13C40, ConfigType::integer},
    {0xA8F13C41, ConfigType::integer}, {0x3685FCE3, ConfigType::hotkey},
};
}

EffectsMenu::EffectsMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities,
                         effects::CameraRuntime& camera)
    : tab_(tab), category_(&tab, u"Effects", {12, 12, 12, 12}, 9, true, identities),
      camera_(camera) {
    require_settings(settings, effects_settings);
    if (&camera.force_dead != value<BooleanConfig>(settings, 0x264E3E21) ||
        &camera.alive_hotkey != value<HotkeyConfig>(settings, 0x3685FCE3))
        throw std::invalid_argument("Effects camera runtime must reference this menu's settings");
    storage_.reserve(std::size(effects_settings));
    bindings_.reserve(std::size(effects_settings));
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto color = [&](std::uint32_t hash) -> ColorPicker& {
        return add<ColorPicker>(category_, hash, value<ColorConfig>(settings, hash), identities);
    };
    auto slider = [&](std::uint32_t hash, std::u16string_view label, std::int32_t minimum,
                      std::int32_t maximum, std::uint32_t suffix) -> Slider& {
        return add<Slider>(category_, hash, label, minimum, maximum,
                           value<IntegerConfig>(settings, hash), true, suffix, 1.0f, identities);
    };
    checkbox(0x461D3721, u"Remove flashbang effects");
    checkbox(0x623BD4F2, u"Remove smoke grenades");
    checkbox(0x3359E29C, u"Remove fog");
    checkbox(0xFBD4768D, u"Remove skybox");
    auto& recoil = add<ComboBox>(category_, 0x70BA27E0, u"Visual recoil adjustment",
                                 value<IntegerConfig>(settings, 0x70BA27E0), false, identities);
    recoil.set_options({{u"Off", 0}, {u"Remove shake", 1}, {u"Remove all", 2}});
    slider(0x9B4090D9, u"Transparent walls", 1, 100, 0x25);
    slider(0x8BED781E, u"Transparent props", 1, 100, 0x25);
    auto& brightness = add<ComboBox>(category_, 0x2E5DD918, u"Brightness adjustment",
                                     value<IntegerConfig>(settings, 0x2E5DD918), false, identities);
    brightness.set_options({{u"Off", 0}, {u"Fullbright", 1}, {u"Night mode", 2}});
    brightness.callbacks.push_back({this, brightness_changed}); // 0x433EFA40
    night_color_ = &color(0x2A0B0DF3);
    night_color_->set_visible(false);
    checkbox(0x171EED00, u"Remove scope overlay");
    checkbox(0xEAFCEAD9, u"Instant scope");
    checkbox(0xA8321370, u"Disable post processing");
    checkbox(0xD35F8540, u"Force third person (alive)")
        .callbacks.push_back({this, alive_camera_changed});
    add<HotkeyWidget>(category_, 0x3685FCE3, value<HotkeyConfig>(settings, 0x3685FCE3), true,
                      identities);
    checkbox(0x264E3E21, u"Force third person (dead)")
        .callbacks.push_back({this, dead_camera_changed});
    checkbox(0x4CBB0E86, u"Disable rendering of teammates");
    checkbox(0xB589F662, u"Disable rendering of ragdolls");
    checkbox(0x3A5285A6, u"Bullet tracers").color = 0xBB82F0F0u;
    color(0x635F47DE);
    auto& impacts = checkbox(0xD54A1CBD, u"Bullet impacts");
    auto& duration = slider(0x4C044F71, u"", 1, 20, 0x73);
    duration.set_visible(false);
    dependencies_[0] = {&duration, &impacts.tied};
    impacts.tied.next = &dependencies_[0];
    auto& molotovs = checkbox(0xC82CBD3B, u"Modulate harmless molotovs");
    color(0xF9200DEF);
    auto& amount = slider(0x921CAA0A, u"", 0, 100, 0x25);
    amount.set_visible(false);
    dependencies_[1] = {&amount, &molotovs.tied};
    molotovs.tied.next = &dependencies_[1];
    slider(0xA8F13C40, u"Force aspect ratio", 0, 200, 0x25);
    slider(0xA8F13C41, u"Thirdperson Distance", 30, 200, 0);
    tab.categories.push_back(&category_);
}
EffectsMenu::~EffectsMenu() {
    std::erase(tab_.categories, &category_);
}

void EffectsMenu::brightness_changed(void* context, Widget& source) { // 0x433B6FB3
    auto& combo = static_cast<ComboBox&>(source);
    static_cast<EffectsMenu*>(context)->night_color_->set_visible(combo.value && *combo.value == 2);
}
void EffectsMenu::alive_camera_changed(void* context, Widget& source) { // 0x433FFD9B / 0x4332C2FB
    auto& checkbox = static_cast<Checkbox&>(source);
    static_cast<EffectsMenu*>(context)->camera_.alive_changed(checkbox.value &&
                                                              *checkbox.value != 0);
}
void EffectsMenu::dead_camera_changed(void* context, Widget&) { // 0x434076B5 / 0x433662E6
    static_cast<EffectsMenu*>(context)->camera_.dead_changed();
}

} // namespace skeet::ui
