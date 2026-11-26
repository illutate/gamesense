#include "skeet/hud/penetration_reticle.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include <cstring>

namespace skeet::hud {
void draw_penetration_reticle(ui::RenderQueue& queue, const esp::ScreenOverlayConfig& config,
                              const PenetrationReticleState& state, void* local,
                              const PenetrationReticleBindings& bindings, Point screen_size) {
    if (!local)
        return;
    std::uint8_t life;
    std::memcpy(&life, static_cast<std::byte*>(local) + bindings.life_state, sizeof(life));
    if (life || !config.penetration_reticle.value || !state.ready)
        return;
    std::int32_t handle;
    std::memcpy(&handle, static_cast<std::byte*>(local) + bindings.active_weapon, sizeof(handle));
    if (handle == -1)
        return;
    auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, handle);
    if (!weapon)
        return;
    std::uint16_t definition;
    std::memcpy(&definition, static_cast<std::byte*>(weapon) + bindings.weapon_definition,
                sizeof(definition));
    if (!lagcomp::history_weapon_is_firearm(definition))
        return;
    const auto x = screen_size.x / 2, y = screen_size.y / 2;
    queue.rectangle({x - 1, y - 1, 3, 3, 0x96000000u});
    queue.rectangle({x, y - 1, 1, 3, state.color});
    queue.rectangle({x - 1, y, 3, 1, state.color});
}
} // namespace skeet::hud
