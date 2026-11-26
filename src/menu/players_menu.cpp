#include "skeet/menu/players_menu.hpp"
#include <algorithm>

namespace skeet::ui {
PlayersMenu::PlayersMenu(Tab& tab, WidgetIdentities& identities, players::State& state,
                         players::Bindings& bindings)
    : tab_(tab), players_(&tab, u"Players", {0, 12, 0, 24}, 0, true, identities), state_(state),
      runtime_bindings_(bindings) {
    storage_.reserve(14);
    bindings_.reserve(14);
    tab.categories.push_back(&players_);
    list_ = &add<Listbox>(players_, 0, u"Player list", Point{158, 425}, &selected_player, false,
                          identities);
    list_->callbacks.push_back({this, selection_changed});
    add<Button>(players_, 0, u"Reset all", Point{158, 28}, WidgetCallback{this, reset_clicked},
                identities);
    adjustments_ = std::make_unique<Category>(&tab, u"Adjustments", GridPosition{12, 12, 0, 24}, 13,
                                              true, identities);
    tab.categories.push_back(adjustments_.get());
    auto checkbox = [&](std::u16string_view label) -> Checkbox* {
        return &add<Checkbox>(*adjustments_, 0, label, nullptr, identities);
    };
    whitelist_ = checkbox(u"Add to whitelist");
    shared_ = checkbox(u"Allow shared ESP updates");
    visuals_ = checkbox(u"Disable visuals");
    priority_ = checkbox(u"High priority");
    force_pitch_ = checkbox(u"Force pitch");
    pitch_ = &add<Slider>(*adjustments_, 0, u"Force pitch value", -90, 90, nullptr, true, 0, 1.0f,
                          identities);
    pitch_->maximum_width = 176;
    pitch_->set_visible(false);
    links_[0] = {pitch_, &force_pitch_->tied};
    force_pitch_->tied.next = &links_[0];
    force_body_ = checkbox(u"Force body yaw");
    body_ = &add<Slider>(*adjustments_, 0, u"Force body yaw value", -60, 60, nullptr, true, 0, 1.0f,
                         identities);
    body_->maximum_width = 176;
    body_->set_visible(false);
    links_[1] = {body_, &force_body_->tied};
    force_body_->tied.next = &links_[1];
    correction_ = checkbox(u"Correction active");
    prefer_body_ = &add<ComboBox>(*adjustments_, 0, u"Override prefer body aim",
                                  static_cast<std::uint32_t*>(nullptr), false, identities);
    prefer_body_->set_options({{u"-", 0}, {u"Off", 1}, {u"On", 2}, {u"Force", 3}});
    safe_ = &add<ComboBox>(*adjustments_, 0, u"Override safe point",
                           static_cast<std::uint32_t*>(nullptr), false, identities);
    safe_->set_options({{u"-", 0}, {u"Off", 1}, {u"On", 2}});
    add<Button>(*adjustments_, 0, u"Apply to all", Point{158, 28},
                WidgetCallback{this, apply_clicked}, identities);
}
PlayersMenu::~PlayersMenu() {
    std::erase(tab_.categories, &players_);
    std::erase(tab_.categories, adjustments_.get());
}
void PlayersMenu::enable_category(Category& category, bool enabled) {
    for (auto* control : std::span(category.elements))
        control->enabled = enabled;
}
void PlayersMenu::activate() {
    const auto list =
        players::build_list(state_, runtime_bindings_, list_->value ? *list_->value : 0);
    if (!list.active) {
        for (auto* category : std::span(tab_.categories))
            enable_category(*category, false);
        list_->set_options(nullptr);
        return;
    }
    std::vector<ListboxOption> options;
    options.reserve(list.entries.size());
    for (const auto& entry : list.entries)
        options.push_back({entry.player_index, entry.name});
    list_->set_options(&options);
    if (list_->options.empty()) {
        for (auto* category : std::span(tab_.categories))
            enable_category(*category, false);
        return;
    }
    enable_category(players_, true);
    list_->set_selection(list.selected_row);
    const auto selected = list_->options[std::size_t(list_->selected_item)].value;
    if (list_->value)
        *list_->value = selected;
    auto& value = state_.records[std::size_t(selected)].adjustments;
    whitelist_->value = &value.whitelist;
    whitelist_->config_loaded();
    shared_->value = list.shared_esp_enabled ? &value.allow_shared_esp : nullptr;
    shared_->config_loaded();
    visuals_->value = &value.disable_visuals;
    visuals_->config_loaded();
    priority_->value = &value.high_priority;
    priority_->config_loaded();
    force_pitch_->value = &value.force_pitch;
    force_pitch_->config_loaded();
    pitch_->value = &value.pitch;
    pitch_->config_loaded();
    force_body_->value = &value.force_body_yaw;
    force_body_->config_loaded();
    body_->value = &value.body_yaw;
    body_->config_loaded();
    correction_->value = &value.correction_active;
    correction_->config_loaded();
    prefer_body_->value = &value.prefer_body_aim;
    prefer_body_->config_loaded();
    safe_->value = &value.safe_point;
    safe_->config_loaded();
    enable_category(*adjustments_, true);
    shared_->enabled = list.shared_esp_enabled;
}
void PlayersMenu::reset_all() {
    if (players::reset_all(state_, runtime_bindings_))
        pitch_->update_value_text();
}
void PlayersMenu::apply_to_all() {
    players::apply_to_all(state_, runtime_bindings_,
                          list_->value ? static_cast<std::uint32_t>(*list_->value) : 0);
}
void PlayersMenu::selection_changed(void* context, Widget&) {
    static_cast<PlayersMenu*>(context)->activate();
}
void PlayersMenu::reset_clicked(void* context, Widget&) {
    static_cast<PlayersMenu*>(context)->reset_all();
}
void PlayersMenu::apply_clicked(void* context, Widget&) {
    static_cast<PlayersMenu*>(context)->apply_to_all();
}
} // namespace skeet::ui
