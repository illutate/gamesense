#include "grenade_live_detail.hpp"

namespace skeet::esp {
using namespace grenade_live_detail;
GrenadeDamageResult GrenadeLiveScene::Storage::damage(void* entity, Vector3 explosion,
                                                      Vector3 selected_origin) {
    const auto& b = bindings;
    const auto& o = raw.bindings().offsets;
    const auto duck = field<float>(entity, offsets.duck_amount);
    const auto height = grenade_player_view_height(duck) * 0.85f;
    const Vector3 distance_point{selected_origin.x, selected_origin.y, height + selected_origin.z};
    if (!(squared(difference(distance_point, explosion)) < 122500.0f)) {
        GrenadeDamageResult result;
        result.sample_position = explosion;
        result.distance_point = distance_point;
        return result;
    }
    Vector3 eye;
    if (field<std::uint8_t>(entity, offsets.alternate_eye)) {
        const auto origin = raw.live_origin(entity);
        const auto offset = field<Vector3>(entity, offsets.view_offset);
        eye = {offset.x + origin.x, origin.y + offset.y, origin.z + offset.z};
    } else {
        const auto origin = field<Vector3>(entity, o.origin);
        const auto eye_duck = field<float>(entity, offsets.duck_amount);
        eye = {origin.x, origin.y, grenade_player_view_height(eye_duck) + origin.z};
    }
    auto top = field<Vector3>(entity, o.origin);
    const auto eye_offset = eye.z - top.z;
    top.z += 71.0f;
    const auto angles = *engine::invoke<const Vector3*>(entity, 11);
    const auto body = field<Vector3>(entity, o.origin);
    auto result = evaluate_he_damage_live_geometry(
        explosion, identity(entity), distance_point,
        make_grenade_damage_samples(body, eye, eye_offset, top, angles, b.math), entity, o.origin,
        raw, b.math);
    if (result.exposure > 0.0f && field<std::int32_t>(entity, offsets.armor) > 0)
        result.damage *= 0.6f;
    return result;
}
GrenadeDamageSelection GrenadeLiveScene::Storage::select_damage(Vector3 explosion,
                                                                const IntegerConfig& threshold) {
    GrenadeDamageSelection selection;
    const auto maximum = bindings.maximum_players;
    for (std::int32_t ordinal = 1; ordinal < maximum; ++ordinal) {
        auto* entity = raw.by_index(ordinal);
        if (!entity || !raw.player(entity))
            continue;
        const auto& o = offsets;
        if (field<std::uint8_t>(entity, o.life_state) ||
            field<std::uint8_t>(entity, o.alternate_eye) ||
            field<std::uint8_t>(entity, o.rejection_cde290d2) ||
            field<std::uint8_t>(entity, o.rejection_af649dac) || !enemy(entity))
            continue;
        Vector3 origin;
        if (!raw.dormant(entity))
            origin = field<Vector3>(entity, raw.bindings().offsets.origin);
        else {
            auto* history = bindings.players.get(std::uint32_t(ordinal));
            if (0.0f >= history->geometry.alpha)
                continue;
            origin = history->geometry.origin;
        }
        const auto result = damage(entity, explosion, origin);
        const auto armor = field<std::int32_t>(entity, offsets.armor);
        const auto required = threshold.value;
        const auto health = engine::invoke<std::int32_t>(entity, 122);
        if (consider_grenade_damage(selection, result.damage, armor, health, required, ordinal))
            break;
    }
    return selection;
}
} // namespace skeet::esp
