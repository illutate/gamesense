#include "skeet/menu/rage_menu.hpp"
#include "skeet/rage/settings.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement requirements[] = {
    {0x4346EDB5, ConfigType::integer}, {0xA4D31376, ConfigType::boolean},
    {0x59BD212A, ConfigType::boolean}, {0x64715C40, ConfigType::boolean},
    {0x86BFE317, ConfigType::boolean}, {0x63660060, ConfigType::boolean},
    {0xA09C0D52, ConfigType::boolean}, {0xD23ED2A7, ConfigType::hotkey},
    {0x4034958C, ConfigType::boolean}, {0x20E889DE, ConfigType::integer},
    {0xD6D22A5A, ConfigType::integer}, {0xA91E4D11, ConfigType::boolean},
    {0xA91E4D12, ConfigType::integer}, {0xA91E4D13, ConfigType::color},
    {0xA91E4D14, ConfigType::color},   {0xA91E4D15, ConfigType::color},
    {0xA91E4D16, ConfigType::boolean}, {0xA91E4D17, ConfigType::color},
    {0x2B385DC7, ConfigType::integer}, {0x141C9037, ConfigType::boolean},
    {0xEC178029, ConfigType::hotkey},  {0x4F67FC33, ConfigType::integer},
    {0xC9FCBE4B, ConfigType::color},   {0x4CB8E4AA, ConfigType::integer},
    {0x8C2E9D71u, ConfigType::boolean},
    {rage::experimental_rage_hash, ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::animation)],
     ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::resolver)],
     ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::records)],
     ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::extrapolated_pose)],
     ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::damage)],
     ConfigType::boolean},
    {rage::experimental_rage_option_hashes[std::size_t(rage::ExperimentalRageOption::hitchance)],
     ConfigType::boolean},
};
}
RageOtherMenu::RageOtherMenu(Tab& tab, ConfigRegistries& settings, WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Other", {12, 12, 0, 24}, 8, true, identities) {
    require_settings(settings, requirements);
    tab.categories.push_back(&category_); //43487B42, before the first bound control.
    category_.movable = category_.resizable = false;
    storage_.reserve(32 + notify_links_.size());
    bindings_.reserve(36 + notify_links_.size());
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto color = [&](std::uint32_t hash) -> ColorPicker& {
        return add<ColorPicker>(category_, hash, value<ColorConfig>(settings, hash), identities);
    };
    auto& accuracy = add<ComboBox>(category_, 0x4346EDB5, u"Accuracy boost",
                                   value<IntegerConfig>(settings, 0x4346EDB5), false, identities);
    accuracy.set_options({{u"Low", 1}, {u"Medium", 3}, {u"High", 2}, {u"Maximum", 4}});
    accuracy.color = 0xBB82F0F0u;
    checkbox(0xA4D31376, u"Anti-aim correction").color = 0xBB82F0F0u;
    checkbox(0x59BD212A, u"Automatic fire");
    checkbox(0x64715C40, u"Automatic penetration").color = 0xBB82F0F0u;
    checkbox(0x86BFE317, u"Silent aim");
    checkbox(0x63660060, u"Remove recoil");
    checkbox(0xA09C0D52, u"Delay shot");
    quick_peek_ = std::make_unique<QuickPeekMenu>(category_, settings, identities);
    const auto peek_bindings = quick_peek_->controls();
    bindings_.insert(bindings_.end(), peek_bindings.begin(), peek_bindings.end());
    add<Label>(category_, 0, u"Duck peek assist", identities);
    add<HotkeyWidget>(category_, 0xD23ED2A7, value<HotkeyConfig>(settings, 0xD23ED2A7), true,
                      identities);
    checkbox(0x4034958C, u"Reduce aim step");
    add<Slider>(category_, 0x20E889DE, u"Reduce aim step limit", 2, 29,
                value<IntegerConfig>(settings, 0x20E889DE), true, 0xB0, 1.0f, identities)
        .set_visible(false);
    add<Slider>(category_, 0xD6D22A5A, u"Maximum FOV", 1, 180,
                value<IntegerConfig>(settings, 0xD6D22A5A), true, 0xB0, 1.0f, identities);
    auto& notifications = checkbox(0xA91E4D11, u"Notifications");
    auto& notify_events = add<MultiSelect>(category_, 0xA91E4D12, u"Events",
                                           value<IntegerConfig>(settings, 0xA91E4D12), identities,
                                           false);
    notify_events.add_option("Hit", 1);
    notify_events.add_option("Miss", 2);
    notify_events.add_option("Purchase", 4);
    auto& hit_label = add<Label>(category_, 0, u"Hit color", identities);
    auto& hit_color = color(0xA91E4D13);
    auto& miss_label = add<Label>(category_, 0, u"Miss color", identities);
    auto& miss_color = color(0xA91E4D14);
    auto& purchase_label = add<Label>(category_, 0, u"Purchase color", identities);
    auto& purchase_color = color(0xA91E4D15);
    notify_links_[0] = {&notify_events, &notify_links_[1]};
    notify_links_[1] = {&hit_label, &notify_links_[2]};
    notify_links_[2] = {&hit_color, &notify_links_[3]};
    notify_links_[3] = {&miss_label, &notify_links_[4]};
    notify_links_[4] = {&miss_color, &notify_links_[5]};
    notify_links_[5] = {&purchase_label, &notify_links_[6]};
    notify_links_[6] = {&purchase_color, &notifications.tied};
    notifications.tied.next = notify_links_.data();
    notifications.config_loaded();
    auto& unsafe_dt = checkbox(0xA91E4D16, u"Unsafe doubletap recharge");
    auto& unsafe_dt_label = add<Label>(category_, 0, u"Unsafe recharge warning", identities);
    auto& unsafe_dt_color = color(0xA91E4D17);
    unsafe_dt_label.set_visible(false);
    unsafe_dt_color.set_visible(false);
    notify_links_[7] = {&unsafe_dt_label, &notify_links_[8]};
    notify_links_[8] = {&unsafe_dt_color, &unsafe_dt.tied};
    unsafe_dt.tied.next = &notify_links_[7];
    unsafe_dt.config_loaded();
    auto& mitigations =
        add<MultiSelect>(category_, 0x2B385DC7, u"Low FPS mitigations",
                         value<IntegerConfig>(settings, 0x2B385DC7), identities, false);
    mitigations.add_option("Force low accuracy boost", 1);
    mitigations.add_option("Disable multipoint: feet", 2);
    mitigations.add_option("Disable multipoint: arms", 4);
    mitigations.add_option("Disable multipoint: legs", 8);
    mitigations.add_option("Disable hitbox: feet", 0x10);
    mitigations.add_option("Lower hit chance precision", 0x40);
    mitigations.add_option("Limit targets per tick", 0x80);
    struct HiddenControl {
        std::uint32_t hash;
        std::u16string_view label;
    };
    using Option = rage::ExperimentalRageOption;
    constexpr auto& hashes = rage::experimental_rage_option_hashes;
    constexpr std::array hidden_controls{
        HiddenControl{0x8C2E9D71u, u"Jitter fix"},
        HiddenControl{rage::experimental_rage_hash, u"Experimental RAGE"},
        HiddenControl{hashes[std::size_t(Option::animation)], u"Animation rebuild"},
        HiddenControl{hashes[std::size_t(Option::resolver)], u"Resolver rebuild"},
        HiddenControl{hashes[std::size_t(Option::records)], u"Record validation"},
        HiddenControl{hashes[std::size_t(Option::extrapolated_pose)], u"Extrapolated pose rebuild"},
        HiddenControl{hashes[std::size_t(Option::damage)], u"Damage model"},
        HiddenControl{hashes[std::size_t(Option::hitchance)], u"Full-seed hitchance"},
    };
    for (const auto& control : hidden_controls) {
        auto& item = checkbox(control.hash, control.label);
        item.color = 0xBB82F0F0u;
        item.set_visible(false);
    }
}
RageOtherMenu::~RageOtherMenu() {
    std::erase(tab_.categories, &category_);
}
} // namespace skeet::ui
