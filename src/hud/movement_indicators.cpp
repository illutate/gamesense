#include "skeet/hud/movement_indicators.hpp"

namespace skeet::hud {
void draw_movement_indicators(ui::RenderQueue& queue, script::Bridge& scripts,
                              const IntegerConfig& feature_indicators, std::uint8_t duck_enabled,
                              const rage::GlobalSettings& rage, std::uint32_t flags,
                              const QuickPeekConfig& quick_peek, MovementIndicatorState state,
                              const esp::RuntimePlayer& local, InputBitmap input,
                              std::int32_t& cursor, const esp::IndicatorLayout& layout,
                              const esp::Projection& projection) {
    if (local.life_state) {
        reset_quick_peek_state(state.quick_peek);
        state.duck_target_52c = 0.0f;
        state.byte_51c = 0;
        state.crouch_active_544 = 0;
        state.value_540 = 27;
        return;
    }
    if ((std::uint32_t(feature_indicators.value) & 16) && duck_enabled) {
        bool active = (flags & 0x40) != 0;
        if (!active)
            active =
                hotkey_active(rage.duck_peek_assist.value.binding, input) || (flags & 0x100) != 0;
        if (active)
            esp::draw_indicator(scripts, queue, cursor, "DUCK", 0xC8FFFFFFu, layout, -1, -1.0f);
    }
    draw_quick_peek(quick_peek, input, state.quick_peek, projection, queue);
}
} // namespace skeet::hud
