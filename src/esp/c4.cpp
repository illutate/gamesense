#include "skeet/esp/c4.hpp"
#include "skeet/esp/player_overlay.hpp"
#include "detail.hpp"

namespace skeet::esp {
using namespace detail;

void draw_dropped_c4(ui::RenderQueue& queue, const WorldEspConfig& config,
                     const Projection& projection, const DroppedC4State& state,
                     std::u16string_view label) {
    if (!config.dropped_weapons.value)
        return;
    if (state.local_player_present && state.local_state == 0)
        return;
    if (state.entity_state != 0)
        return;
    Point point;
    if (!project_point(projection, state.origin, point))
        return;
    queue.text(point.x, point.y, config.bomb.color.value, 0x14, 0, label);
    if (config.dropped_weapons.value & 0x10) {
        auto distance = decimal(distance_in_feet(state.view_origin.x - state.origin.x,
                                                 state.view_origin.y - state.origin.y,
                                                 state.view_origin.z - state.origin.z));
        distance += u" FT";
        queue.text(point.x, add(point.y, 10), config.bomb.color.value, 0x14, 0, distance);
    }
}

float bomb_damage(const ActiveBomb* bomb, const BombDamageTarget& target, float peak_damage,
                  BombTraceProvider provider) {
    if (!bomb || !bomb->ticking)
        return 0.0f;
    const Vector3 start{bomb->origin.x, bomb->origin.y, bomb->origin.z + 8.0f};
    const Vector3 delta{start.x - start.x, start.y - start.y, (start.z - 40.0f) - start.z};
    const auto swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
    const auto ground =
        provider.trace(provider.context, {start, delta, swept}, 0x200400Bu, bomb->entity);
    auto origin = bomb->origin;
    if (ground.fraction != 1.0f) {
        origin.x = ground.plane_normal.x * 0.6f + ground.end_position.x;
        origin.y = ground.plane_normal.y * 0.6f + ground.end_position.y;
        origin.z = ground.plane_normal.z * 0.6f + ground.end_position.z;
    }
    const auto radius = (peak_damage * 3.5f) * std::bit_cast<float>(0x3EAAAAABu);
    const auto denominator = (radius + radius) * radius;
    const auto x = (target.origin.x + target.view_offset.x) - origin.x;
    const auto y = (target.origin.y + target.view_offset.y) - origin.y;
    const auto z = (target.origin.z + target.view_offset.z) - (origin.z + 1.0f);
    const auto damage = std::exp(-((y * y + x * x) + z * z) / denominator) * peak_damage;
    return target.armor > 0 ? damage * 0.5f : damage;
}

} // namespace skeet::esp
