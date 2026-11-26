#include "skeet/esp/grenade_prediction_handler.hpp"
#include <array>

namespace skeet::esp {
namespace {
#if defined(_M_IX86) || defined(__i386__)
void __fastcall grenade_prediction_entry(void* endpoint, ui::RenderQueue* queue, void*,
                                         std::int32_t*) {
#else
void grenade_prediction_entry(void* endpoint, ui::RenderQueue* queue, void*, std::int32_t*) {
#endif
    auto& handler =
        *static_cast<GrenadePredictionHudHandler*>(static_cast<hud::HudFrameCallback*>(endpoint));
    render_grenade_prediction(*queue, handler.config, handler.owner, handler.frame, handler.scene);
}
const std::array<hud::HudFrameEntry, 1> grenade_prediction_table{grenade_prediction_entry};
}
GrenadePredictionHudHandler::GrenadePredictionHudHandler(engine::OwnerRegistryBinding registry,
                                                         const GrenadeEspConfig& config_input,
                                                         GrenadeTrajectoryOwner& owner_input,
                                                         const GrenadeTrajectoryFrame& frame_input,
                                                         const GrenadePredictionScene& scene_input)
    : hud::HudFrameCallback{nullptr}, config(config_input), owner(owner_input), frame(frame_input),
      scene(scene_input) {
    hud::register_hud_frame(registry, this); //43486787 precedes secondary-vtable setup.
    vtable = grenade_prediction_table.data();
}
} // namespace skeet::esp
