#include "skeet/client_menu.hpp"
#include "config_binding.hpp"
#include <algorithm>

namespace skeet::ui {
using namespace menu_detail;
namespace {
constexpr Requirement colored_models_settings[] = {
    {0xC51E0D3D, ConfigType::boolean}, {0xF6B67DE5, ConfigType::boolean},
    {0x1570AF7C, ConfigType::boolean}, {0x57BF65EC, ConfigType::boolean},
    {0x6670DB4D, ConfigType::boolean}, {0x96DB2E1C, ConfigType::boolean},
    {0x5E0E2558, ConfigType::boolean}, {0x8CCAF8DC, ConfigType::boolean},
    {0xE8407B64, ConfigType::boolean}, {0xDA411D58, ConfigType::boolean},
    {0x92EBAD93, ConfigType::boolean}, {0x8E0781AE, ConfigType::boolean},
    {0x67F8885C, ConfigType::boolean}, {0xE217C264, ConfigType::boolean},
    {0xFE06341D, ConfigType::color},   {0xD4699245, ConfigType::color},
    {0x9FF240E4, ConfigType::color},   {0x2CEFC91C, ConfigType::color},
    {0x04AD8938, ConfigType::color},   {0xF3D8F0D1, ConfigType::color},
    {0x9201E7ED, ConfigType::color},   {0x9436ECB4, ConfigType::color},
    {0xA98CEAE8, ConfigType::color},   {0xF50A4300, ConfigType::color},
    {0xC2DBF0A4, ConfigType::color},   {0x8B122392, ConfigType::color},
    {0x5A93F490, ConfigType::color},   {0x2926F5DE, ConfigType::color},
    {0x75A868A4, ConfigType::color},   {0x1C678B92, ConfigType::color},
    {0x83BE3FA7, ConfigType::color},   {0x6D66C717, ConfigType::color},
    {0xC2140C28, ConfigType::color},   {0x84830790, ConfigType::color},
    {0x044F7095, ConfigType::integer}, {0xE85380FA, ConfigType::integer},
    {0x3B28B0E5, ConfigType::integer}, {0x3EA36C3A, ConfigType::integer},
    {0x3FC208C1, ConfigType::integer}, {0x17C5B91A, ConfigType::integer},
    {0x1A07B48F, ConfigType::integer}, {0xB7360BA6, ConfigType::integer},
    {0x2A1FA11A, ConfigType::integer}, {0x118A8443, ConfigType::integer},
};
const std::vector<ComboBoxOption> materials = {
    {u"Default", 0}, {u"Solid", 1},  {u"Shaded", 2},   {u"Metallic", 3},
    {u"Glow", 4},    {u"Bubble", 5}, {u"Original", 6},
};
}

ColoredModelsMenu::ColoredModelsMenu(Tab& tab, ConfigRegistries& settings,
                                     WidgetIdentities& identities)
    : tab_(tab), category_(&tab, u"Colored models", {0, 12, 13, 11}, 16, true, identities) {
    require_settings(settings, colored_models_settings);
    storage_.reserve(std::size(colored_models_settings));
    bindings_.reserve(std::size(colored_models_settings));
    auto checkbox = [&](std::uint32_t hash, std::u16string_view label) -> Checkbox& {
        return add<Checkbox>(category_, hash, label, value<BooleanConfig>(settings, hash),
                             identities);
    };
    auto color = [&](std::uint32_t hash) -> ColorPicker& {
        return add<ColorPicker>(category_, hash, value<ColorConfig>(settings, hash), identities);
    };
    auto group = [&](std::size_t index, std::u16string_view label, std::uint32_t enabled_hash,
                     std::uint32_t primary_color, std::uint32_t material_hash,
                     std::uint32_t secondary_color, std::uint32_t behind_hash = 0,
                     std::uint32_t behind_color = 0,
                     std::u16string_view behind_label = u"") -> MaterialGroup& {
        auto& current = groups_[index];
        current.primary = &checkbox(enabled_hash, label);
        current.primary->callbacks.push_back({&current, enabled_changed});
        color(primary_color);
        if (behind_hash) {
            current.behind_wall = &checkbox(behind_hash, behind_label);
            current.behind_wall->callbacks.push_back({&current, enabled_changed});
            color(behind_color);
        }
        current.material =
            &add<ComboBox>(category_, material_hash, u"",
                           value<IntegerConfig>(settings, material_hash), false, identities);
        current.material->set_options(materials);
        current.material->callbacks.push_back({&current, material_changed});
        current.secondary_color = &color(secondary_color);
        return current;
    };
    auto& player = group(0, u"Player", 0xC51E0D3D, 0xFE06341D, 0x044F7095, 0x9FF240E4, 0xF6B67DE5,
                         0xD4699245, u"Player behind wall");
    enabled_changed(&player, *player.primary); // 0x43407C86
    auto& teammate = group(1, u"Teammate", 0x1570AF7C, 0x2CEFC91C, 0xE85380FA, 0xF3D8F0D1,
                           0x57BF65EC, 0x04AD8938, u"Teammate behind wall");
    enabled_changed(&teammate, *teammate.primary); // 0x433FB6B1
    auto& local = group(2, u"Local player", 0x6670DB4D, 0x9201E7ED, 0x3B28B0E5, 0x9436ECB4);
    enabled_changed(&local, *local.primary); // 0x4337CACF
    auto& transparency =
        add<MultiSelect>(category_, 0x3EA36C3A, u"Local player transparency",
                         value<IntegerConfig>(settings, 0x3EA36C3A), identities, false);
    transparency.add_option("Grenade", 1);
    transparency.add_option("Scoped", 2);
    auto& fake = group(3, u"Local player fake", 0x96DB2E1C, 0xA98CEAE8, 0x3FC208C1, 0xF50A4300);
    fake.primary->color = 0xBB82F0F0u;
    enabled_changed(&fake, *fake.primary); // 0x433AB16B
    auto& on_shot = group(4, u"On shot", 0x5E0E2558, 0xC2DBF0A4, 0x17C5B91A, 0x8B122392);
    on_shot.duration =
        &add<Slider>(category_, 0x1A07B48F, u"", 2, 100, value<IntegerConfig>(settings, 0x1A07B48F),
                     true, 0x73u, 0.1f, identities);
    enabled_changed(&on_shot, *on_shot.primary); // 0x433CE8BA
    checkbox(0x8CCAF8DC, u"Ragdolls");
    auto& hands = group(5, u"Hands", 0xE8407B64, 0x5A93F490, 0xB7360BA6, 0x2926F5DE);
    enabled_changed(&hands, *hands.primary); // 0x433E520A
    auto& viewmodel = group(6, u"Weapon viewmodel", 0xDA411D58, 0x75A868A4, 0x2A1FA11A, 0x1C678B92);
    enabled_changed(&viewmodel, *viewmodel.primary); // 0x433F9C51
    auto& weapons = group(7, u"Weapons", 0x92EBAD93, 0x83BE3FA7, 0x118A8443, 0x6D66C717);
    enabled_changed(&weapons, *weapons.primary); // 0x433A121B
    checkbox(0x8E0781AE, u"Disable model occlusion").color = 0xBB82F0F0u;
    checkbox(0x67F8885C, u"Shadow").color = 0xBB82F0F0u;
    color(0xC2140C28);
    checkbox(0xE217C264, u"Props");
    color(0x84830790);
    tab.categories.push_back(&category_);
}
ColoredModelsMenu::~ColoredModelsMenu() {
    std::erase(tab_.categories, &category_);
}

bool ColoredModelsMenu::active(const MaterialGroup& group) noexcept {
    return (group.primary->value && *group.primary->value != 0) ||
           (group.behind_wall && group.behind_wall->value && *group.behind_wall->value != 0);
}
void ColoredModelsMenu::material_changed(void* context, Widget&) {
    // Equivalent owner-specialized bodies: 0x433ED78B / 0x433A0023 /
    // 0x433AFD35 / 0x43322804 / 0x43329AD5 / 0x4335933C /
    // 0x433593AF / 0x433D6599. Test the values, independent of widget visibility.
    auto& group = *static_cast<MaterialGroup*>(context);
    const auto material = group.material->value ? *group.material->value : 0;
    group.secondary_color->set_visible(active(group) && material >= 2 && material <= 5);
}
void ColoredModelsMenu::enabled_changed(void* context, Widget& source) {
    auto& group = *static_cast<MaterialGroup*>(context);
    const auto enabled = active(group);
    group.material->set_visible(enabled);
    material_changed(context, source);
    if (group.duration)
        group.duration->set_visible(enabled); // 0x433CE8EF..0x433CE8FC
}

} // namespace skeet::ui
