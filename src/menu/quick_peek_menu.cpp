#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement quick_peek_settings[] = {
    {0x141C9037, ConfigType::boolean}, {0xEC178029, ConfigType::hotkey},
    {0x4F67FC33, ConfigType::integer}, {0xC9FCBE4B, ConfigType::color},
    {0x4CB8E4AA, ConfigType::integer},
};
}

QuickPeekMenu::QuickPeekMenu(Category& category, ConfigRegistries& settings,
                             WidgetIdentities& identities)
    : category_(category) {
    require_settings(settings, quick_peek_settings);
    storage_.reserve(std::size(quick_peek_settings));
    bindings_.reserve(std::size(quick_peek_settings));
    auto& checkbox = add<Checkbox>(category_, 0x141C9037, u"Quick peek assist",
                                   value<BooleanConfig>(settings, 0x141C9037), identities);
    add<HotkeyWidget>(category_, 0xEC178029, value<HotkeyConfig>(settings, 0xEC178029), true,
                      identities);
    auto& mode = add<MultiSelect>(category_, 0x4F67FC33, u"Quick peek assist mode",
                                  value<IntegerConfig>(settings, 0x4F67FC33), identities);
    mode.add_option("Retreat on shot", 1);
    mode.add_option("Retreat on key release", 2);
    mode.set_visible(false);
    auto& color = add<ColorPicker>(category_, 0xC9FCBE4B, value<ColorConfig>(settings, 0xC9FCBE4B),
                                   identities);
    color.set_visible(false);
    auto& distance =
        add<Slider>(category_, 0x4CB8E4AA, u"Quick peek assist distance", 16, 201,
                    value<IntegerConfig>(settings, 0x4CB8E4AA), true, 0x6E0069u, 1.0f, identities);
    distance.value_labels.emplace(201, L"\u221E");
    if (*distance.value == 201)
        distance.update_value_text(); // 0x43402B1B..0x43402B2B
    distance.set_visible(false);
    dependencies_[0] = {&mode, &dependencies_[1]};
    dependencies_[1] = {&color, &dependencies_[2]};
    dependencies_[2] = {&distance, &checkbox.tied};
    checkbox.tied.next = &dependencies_[0];
}
QuickPeekMenu::~QuickPeekMenu() {
    detach(category_);
}

} // namespace skeet::ui
