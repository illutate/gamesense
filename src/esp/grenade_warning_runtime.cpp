#include "skeet/esp/grenade_warning_owner.hpp"

namespace skeet::esp {
void draw_grenade_warning_records(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                                  const Projection& projection, GrenadeWarningOwner& owner,
                                  GrenadeWarningEngine& engine, const GrenadeWarningFrame& frame,
                                  GrenadeWarningTexture explosive, GrenadeWarningTexture fire) {
    if (!config.proximity_warning.value || !frame.local_present || frame.life_state != 0)
        return;
    if (frame.movement_type == 10 && (frame.observer_mode == 4 || frame.observer_mode == 5))
        return;
    for (auto& record : owner.records) {
        GrenadeWarningSnapshot snapshot;
        GrenadeWarningEntity entity{};
        if (record.handle != 0xffffffffu && engine.snapshot_for_handle(record.handle, snapshot)) {
            entity = {true, bool(snapshot.flags & 0x20u), snapshot.explosion_effect_tick,
                      snapshot.origin, snapshot.fire_cells};
        }
        draw_grenade_warnings(queue, config, projection, {&record, 1}, {&entity, 1}, frame,
                              explosive, fire);
    }
}
} // namespace skeet::esp
