#include "skeet/anti_aim/menu_dependencies.hpp"
#include <bit>

namespace skeet::anti_aim {
namespace {
std::int32_t current(const ui::ComboBox& source) noexcept {
    return source.value ? std::bit_cast<std::int32_t>(*source.value) : 0;
}
}
void pitch_changed(MenuDependencies& widgets, const ui::ComboBox& source) {
    const auto pitch = current(source);
    if (pitch > 6)
        return;
    widgets.custom_pitch.set_visible(pitch == 6);
}
void yaw_changed(MenuDependencies& widgets, const ui::ComboBox& source) {
    const auto yaw = current(source);
    if (yaw >= 6)
        return;
    if (yaw) {
        widgets.yaw_offset.value = &widgets.yaw_offsets[yaw];
        widgets.yaw_offset.value_changed();
    }
    widgets.yaw_offset.set_visible(yaw != 0);
    widgets.yaw_jitter.set_visible(yaw != 0);
    widgets.yaw_jitter_offset.set_visible(yaw != 0 && current(widgets.yaw_jitter) != 0);
}
void jitter_changed(MenuDependencies& widgets, const ui::ComboBox& source,
                    const ui::ComboBox& yaw) {
    const auto jitter = current(source);
    if (source.value && jitter >= 5)
        return;
    widgets.yaw_jitter_offset.set_visible(jitter != 0 && current(yaw) != 0);
}
void body_yaw_changed(MenuDependencies& widgets, const ui::ComboBox& source) {
    const auto body_yaw = current(source);
    if (body_yaw >= 4)
        return;
    widgets.freestanding_body_yaw.set_visible(body_yaw != 0);
    if (body_yaw != 0 && body_yaw != 1) {
        widgets.body_yaw_offset.value = &widgets.body_yaw_offsets[body_yaw];
        widgets.body_yaw_offset.value_changed();
    }
    widgets.body_yaw_offset.set_visible(body_yaw != 0 && body_yaw != 1);
}
} // namespace skeet::anti_aim
