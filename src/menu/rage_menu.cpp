#include "skeet/menu/rage_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement requirements[] = {
    {0x23060EC5, ConfigType::boolean}, {0x861E7F9A, ConfigType::hotkey},
    {0x65E86322, ConfigType::integer}, {0x5B900854, ConfigType::integer},
    {0xBFABB4A0, ConfigType::integer}, {0x29E2C8ED, ConfigType::hotkey},
    {0xA25029BA, ConfigType::integer}, {0x3F5E5553, ConfigType::integer},
    {0x18C0E6BE, ConfigType::integer}, {0xB80303B4, ConfigType::boolean},
    {0x7EB3A142, ConfigType::hotkey},  {0x7DC413FE, ConfigType::integer},
    {0xF5A00B3B, ConfigType::boolean}, {0x333C1DBE, ConfigType::hotkey},
    {0xECBBD134, ConfigType::integer}, {0x2B54548D, ConfigType::boolean},
    {0x16C91A85, ConfigType::integer}, {0x02F59674, ConfigType::hotkey},
    {0x9BBD48EA, ConfigType::boolean}, {0xC06FD8CC, ConfigType::boolean},
    {0x1AC79342, ConfigType::hotkey},  {0x11195AD9, ConfigType::integer},
    {0x0889939F, ConfigType::boolean}, {0x67C9986D, ConfigType::hotkey},
    {0x31E2D7DD, ConfigType::integer}, {0x3F69C17C, ConfigType::integer},
    {0x2B56E048, ConfigType::integer}, {0x9FBDD0E6, ConfigType::integer},
    {0x9E2B2E76, ConfigType::boolean},
};
void hitboxes(MultiSelect& control) {
    control.add_option("Head", 2);
    control.add_option("Chest", 4);
    control.add_option("Stomach", 8);
    control.add_option("Arms", 0x30);
    control.add_option("Legs", 0xC0);
    control.add_option("Feet", 0x200);
}
}
RageAimbotMenu::RageAimbotMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Aimbot", {0, 12, 0, 24}, 24, true, identities) {
    require_settings(settings, requirements);
    tab.categories.push_back(&category_); //43487180, before the first bound control.
    category_.movable = category_.resizable = false;
    storage_.reserve(31);
    bindings_.reserve(31);
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto key = [&](std::uint32_t hash) -> HotkeyWidget& {
        return add<HotkeyWidget>(category_, hash, value<HotkeyConfig>(settings, hash), true,
                                 identities);
    };
    auto multi = [&](std::uint32_t hash, std::u16string_view label,
                     bool require_one = false) -> MultiSelect& {
        return add<MultiSelect>(category_, hash, label, value<IntegerConfig>(settings, hash),
                                identities, require_one);
    };
    auto slider = [&](std::uint32_t hash, std::u16string_view label, int minimum, int maximum,
                      std::uint32_t suffix = 0) -> Slider& {
        return add<Slider>(category_, hash, label, minimum, maximum,
                           value<IntegerConfig>(settings, hash), true, suffix, 1.0f, identities);
    };
    auto tie = [&](std::size_t index, Checkbox& source, Widget& target) {
        target.set_visible(false);
        dependencies_[index] = {&target, &source.tied};
        auto* end = &source.tied;
        while (end->next != &source.tied)
            end = end->next;
        end->next = &dependencies_[index];
    };
    checkbox(0x23060EC5, u"Enabled");
    key(0x861E7F9A);
    auto& selection = add<ComboBox>(category_, 0x65E86322, u"Target selection",
                                    value<IntegerConfig>(settings, 0x65E86322), false, identities);
    selection.set_options({{u"Highest damage", 2},
                           {u"Cycle", 0},
                           {u"Cycle (2x)", 3},
                           {u"Near crosshair", 1},
                           {u"Best hit chance", 6}});
    auto& selected = multi(0x5B900854, u"Target hitbox", true);
    hitboxes(selected);
    selected.callbacks.push_back({this, hitboxes_changed});
    auto& points = multi(0xBFABB4A0, u"Multi-point");
    points.color = 0xBB82F0F0u;
    hitboxes(points);
    points.callbacks.push_back({this, multipoint_changed});
    key(0x29E2C8ED);
    multipoint_scale_ = &slider(0xA25029BA, u"Multi-point scale", 24, 100, 0x25);
    multipoint_scale_->set_visible(false);
    multipoint_scale_->value_labels.emplace(24, L"Auto");
    if (*multipoint_scale_->value == 24)
        multipoint_scale_->update_value_text();
    auto& chance = slider(0x3F5E5553, u"Minimum hit chance", 0, 100, 0x25);
    chance.value_labels.emplace(0, L"Off");
    if (*chance.value == 0)
        chance.update_value_text();
    add<RageDamageSlider>(category_, 0x18C0E6BE, u"Minimum damage",
                          value<IntegerConfig>(settings, 0x18C0E6BE), identities);
    auto& override = checkbox(0xB80303B4, u"Minimum damage override");
    key(0x7EB3A142);
    auto& override_damage = add<RageDamageSlider>(
        category_, 0x7DC413FE, u"", value<IntegerConfig>(settings, 0x7DC413FE), identities);
    tie(0, override, override_damage);
    checkbox(0xF5A00B3B, u"Prefer safe point");
    add<Label>(category_, 0, u"Force safe point", identities);
    key(0x333C1DBE);
    auto& unsafe = multi(0xECBBD134, u"Avoid unsafe hitboxes");
    hitboxes(unsafe);
    prefer_body_ = &checkbox(0x2B54548D, u"Prefer body aim");
    prefer_body_->set_visible(false);
    auto& disablers = multi(0x16C91A85, u"Prefer body aim disablers");
    disablers.add_option("Low inaccuracy", 1);
    disablers.add_option("Target shot fired", 2);
    disablers.add_option("Target resolved", 4);
    disablers.add_option("Safe point headshot", 8);
    tie(1, *prefer_body_, disablers);
    add<Label>(category_, 0, u"Force body aim", identities);
    key(0x02F59674);
    checkbox(0x9BBD48EA, u"Force body aim on peek");
    auto& stop = checkbox(0xC06FD8CC, u"Quick stop");
    key(0x1AC79342);
    auto& stop_options = multi(0x11195AD9, u"");
    stop_options.add_option("Early", 1);
    stop_options.add_option("Slow motion", 4);
    stop_options.add_option("Duck", 2);
    stop_options.add_option("Fake duck", 0x20);
    stop_options.add_option("Move between shots", 8);
    stop_options.add_option("Ignore molotov", 0x10);
    stop_options.add_option("Taser", 0x40);
    stop_options.add_option("Jump scout", 0x80);
    tie(2, stop, stop_options);
    auto& double_tap = checkbox(0x0889939F, u"Double tap");
    double_tap.color = 0xBB82F0F0;
    key(0x67C9986D);
    auto& mode = add<ComboBox>(category_, 0x31E2D7DD, u"",
                               value<IntegerConfig>(settings, 0x31E2D7DD), false, identities);
    mode.set_options({{u"Offensive", 0}, {u"Defensive", 1}});
    tie(3, double_tap, mode);
    tie(4, double_tap, slider(0x3F69C17C, u"Double tap hit chance", 0, 100, 0x25));
    tie(5, double_tap, slider(0x2B56E048, u"Double tap fake lag limit", 1, 10));
    auto& double_stop = multi(0x9FBDD0E6, u"Double tap quick stop");
    double_stop.add_option("Slow motion", 4);
    double_stop.add_option("Duck", 2);
    double_stop.add_option("Move between shots", 8);
    tie(6, double_tap, double_stop);
    checkbox(0x9E2B2E76, u"Automatic scope");
}
RageAimbotMenu::~RageAimbotMenu() {
    std::erase(tab_.categories, &category_);
}
void RageAimbotMenu::hitboxes_changed(void* context, Widget& source) { // 0x433CEE44 / 0x433EFA51
    auto& self = *static_cast<RageAimbotMenu*>(context);
    auto* selected = static_cast<MultiSelect&>(source).value;
    const auto mask = selected ? *selected : 0;
    const bool allowed = (mask & 2) && (mask & 0xE) != 2;
    if (!allowed)
        self.prefer_body_->set_value(false);
    self.prefer_body_->set_visible(allowed);
}
void RageAimbotMenu::multipoint_changed(void* context, Widget& source) { // 0x43348202 / 0x43369A9B
    auto* selected = static_cast<MultiSelect&>(source).value;
    static_cast<RageAimbotMenu*>(context)->multipoint_scale_->set_visible(selected &&
                                                                          *selected != 0);
}
} // namespace skeet::ui
