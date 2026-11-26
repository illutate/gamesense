#include "skeet/anti_aim/eligibility.hpp"

namespace skeet::anti_aim {
bool hud_chat_active(const EligibilityBindings& bindings) {
    if (!bindings.find_hud)
        return false;
    auto* chat = bindings.find_hud(bindings.hud_context, "CCSGO_HudChat");
    return chat && engine::invoke<bool>(chat, 13);
}
bool command_eligible(const EligibilitySettings& settings, const EligibilityFrame& frame,
                      const EligibilityGuard& guard, std::int32_t command_handle,
                      const EligibilityBindings& bindings, std::uint32_t& flags) {
    flags = (flags & ~4u) | ((frame.flags >> 4) & 4u);
    if (!settings.enabled || (frame.secondary_flags & 4u) ||
        (settings.pitch == 0 && settings.yaw == 0 && settings.body_yaw == 0))
        return false;
    if (guard.shared_player_available && guard.guard &&
        guard.current_time - guard.reference_time > 0.5f)
        return false;
    if (command_handle != -1 && engine::invoke<bool>(bindings.input_system, 15, command_handle) &&
        !engine::invoke<bool>(bindings.engine_client, 11) && !hud_chat_active(bindings))
        return false;
    return true;
}
bool command_uses_cached_angles(bool player_state, std::int32_t movement_type,
                                std::uint32_t frame_flags, std::uint32_t buttons,
                                ProximityEngine& proximity, ProximityEntity local) {
    if (player_state)
        return true;
    return (frame_flags & 0x200000u) && (buttons & 0x20u) && movement_type == 3 &&
           any_nearby_entity(proximity, local);
}
} // namespace skeet::anti_aim
