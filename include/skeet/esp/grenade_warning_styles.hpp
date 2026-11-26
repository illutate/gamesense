#pragma once
#include "skeet/esp/grenade_prediction.hpp"
#include <span>

namespace skeet::esp {

struct StylizedWarningInput {
    const GrenadeEspConfig& config;
    const Projection& projection;
    const GrenadeWarningRecord& record;
    float distance;
    std::int32_t screen_width;
    std::int32_t screen_height;
    std::int32_t local_health;
    float current_time;
    GrenadeWarningTexture texture;
};

void draw_stylized_grenade_warning(ui::RenderQueue& queue, const StylizedWarningInput& input);

void draw_molotov_area_hull(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                            const Projection& projection, std::span<const Vector3> world_cells,
                            bool harmful, float fade_fraction);

void draw_smoke_area(ui::RenderQueue& queue, const GrenadeEspConfig& config,
                     const Projection& projection, Vector3 origin, float fade_fraction);

} // namespace skeet::esp
