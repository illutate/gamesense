#include "skeet/menu/scripts_menu.hpp"
#include <bit>
#include <charconv>
#include <ctime>

namespace skeet::ui {

std::uint64_t script_menu_time(void*) {
    return static_cast<std::uint64_t>(std::time(nullptr));
}

std::u16string script_update_age(std::uint64_t modified_time, std::uint64_t now) {
    std::uint32_t amount = 0;
    std::u16string_view unit = u"seconds";
    if (std::bit_cast<std::int64_t>(now) > std::bit_cast<std::int64_t>(modified_time)) {
        amount = static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(modified_time);
        if (amount > 60) {
            amount /= 60;
            unit = u"minutes";
            if (amount > 60) {
                amount /= 60;
                unit = u"hours";
                if (amount > 48) {
                    amount /= 24;
                    unit = u"days";
                }
            }
        }
    }
    std::array<char, 16> number{};
    const auto conversion = std::to_chars(number.data(), number.data() + number.size(), amount);
    std::u16string result = u"Updated ";
    for (auto* p = number.data(); p != conversion.ptr; ++p)
        result.push_back(char16_t(*p));
    result.push_back(u' ');
    result += unit;
    result += u" ago";
    return result;
}

ScriptsCategory::ScriptsCategory(Tab& tab, WidgetIdentities& identities)
    : Category(&tab, u"Lua", {12, 12, 0, 24}, 8, true, identities) {}

void ScriptsCategory::resize(const SliderMetrics& metrics) {
    scripts->geometry.default_size = {158, 325};
    scripts->geometry.bounds.width = 158;
    scripts->geometry.bounds.height = 325;
    scripts->dpi_changed(find_menu()->scale, metrics);
    reflow();
    Category::resize(metrics);
    scripts->update_scroll();
}

} // namespace skeet::ui
