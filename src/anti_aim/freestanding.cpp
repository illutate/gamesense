#include "skeet/anti_aim/freestanding.hpp"
#include "skeet/movement/steering_math.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <immintrin.h>
#include <limits>

namespace skeet::anti_aim {
namespace {
float neg(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
BallisticVector subtract(const BallisticVector& a, const BallisticVector& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
BallisticVector advance(const BallisticVector& a, const BallisticVector& b, float distance) {
    return {b[0] * distance + a[0], b[1] * distance + a[1], b[2] * distance + a[2]};
}
float length(const BallisticVector& a) {
    return std::sqrt((a[0] * a[0] + a[1] * a[1]) + a[2] * a[2]);
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t difference(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
// Local filter vtable 4346EEBC: accepts=false, type=0; source/last-hit
// setters forward to the registered filter implementation at 43475B90.
class FreestandingFilterEngine final : public BallisticEngine {
    BallisticEngine& engine_;

public:
    explicit FreestandingFilterEngine(BallisticEngine& engine) : engine_(engine) {}
    std::int32_t filter_type(const BallisticFilter&) override {
        return 0;
    }
    bool accepts(const BallisticFilter&, BallisticEntity, std::uint32_t) override {
        return false;
    }
    void bind_source(const BallisticFilter& f, BallisticEntity e) override {
        engine_.bind_source(f, e);
    }
    void bind_last_hit(const BallisticFilter& f, BallisticEntity e) override {
        engine_.bind_last_hit(f, e);
    }
    std::uint32_t point_contents(const BallisticVector& p, std::uint32_t mask,
                                 bool world) override {
        return engine_.point_contents(p, mask, world);
    }
    void trace_ray(const BallisticTraceRequest& r, BallisticTrace& t) override {
        engine_.trace_ray(r, t);
    }
    void clip_ray_to_entity(const BallisticRay& r, std::uint32_t m, BallisticEntity e,
                            BallisticTrace& t) override {
        engine_.clip_ray_to_entity(r, m, e, t);
    }
    PenetrationSurface surface(std::int16_t p) override {
        return engine_.surface(p);
    }
    BallisticEntity entity(std::int32_t i) override {
        return engine_.entity(i);
    }
    std::int32_t maximum_clients() override {
        return engine_.maximum_clients();
    }
    BallisticEntityInfo entity_info(BallisticEntity e) override {
        return engine_.entity_info(e);
    }
    BallisticEntityInfo clip_entity_info(BallisticEntity e) override {
        return engine_.clip_entity_info(e);
    }
    BallisticBreakableInfo breakable_info(BallisticEntity e) override {
        return engine_.breakable_info(e);
    }
    BallisticModelInfo model_info(std::int32_t i) override {
        return engine_.model_info(i);
    }
    std::uint8_t damage_mode(BallisticEntity e) override {
        return engine_.damage_mode(e);
    }
    void set_damage_mode(BallisticEntity e, std::uint8_t m) override {
        engine_.set_damage_mode(e, m);
    }
    bool has_engine_breakable_callback() override {
        return engine_.has_engine_breakable_callback();
    }
    bool engine_breakable(BallisticEntity e) override {
        return engine_.engine_breakable(e);
    }
    BallisticRules rules() override {
        return engine_.rules();
    }
};
}
void build_freestanding_rays(std::array<FreestandingRay, 12>& rays,
                             const std::array<FreestandingScore, 3>& scores,
                             const BallisticVector& start, const BallisticVector& target,
                             const BallisticVector& side) {
    const auto left12 = advance(start, scores[0].direction, 12.0f);
    const auto right12 = advance(start, scores[1].direction, 12.0f);
    const auto left22 = advance(start, scores[0].direction, 22.0f);
    const auto right22 = advance(start, scores[1].direction, 22.0f);
    BallisticVector offset{side[0] * 50.0f, side[1] * 50.0f, side[2] * 50.0f};
    const BallisticVector positive{offset[0] + target[0], offset[1] + target[1],
                                   offset[2] + target[2]};
    const BallisticVector negative{target[0] - offset[0], target[1] - offset[1],
                                   target[2] - offset[2]};
    rays[0] = {true, left12, target};
    rays[1] = {true, right12, target};
    rays[2] = {true, start, target};
    rays[3] = {true, left22, target};
    rays[4] = {true, right22, target};
    rays[5].enabled = false;
    rays[6] = {true, left22, positive};
    rays[7] = {true, right22, positive};
    rays[8] = {true, start, positive};
    rays[9] = {true, left22, negative};
    rays[10] = {true, right22, negative};
    rays[11] = {true, start, negative};
}
bool freestanding_difference(const std::array<FreestandingScore, 3>& scores) noexcept {
    if (scores[1].damage == scores[0].damage &&
        scores[1].maximum_distance == scores[0].maximum_distance)
        return false;
    auto minimum_damage = std::numeric_limits<std::int32_t>::max(),
         maximum_damage = std::numeric_limits<std::int32_t>::min();
    auto minimum_distance = minimum_damage, maximum_distance = maximum_damage;
    for (const auto& score : scores) {
        minimum_damage = std::min(minimum_damage, score.damage);
        maximum_damage = std::max(maximum_damage, score.damage);
        minimum_distance = std::min(minimum_distance, score.maximum_distance);
        maximum_distance = std::max(maximum_distance, score.maximum_distance);
    }
    return (minimum_damage == 0 && maximum_damage != 0) ||
           (minimum_distance < 50 && (difference(maximum_damage, minimum_damage) > 5 ||
                                      difference(maximum_distance, minimum_distance) > 25));
}
std::int32_t select_freestanding_side(const std::array<FreestandingScore, 3>& scores) noexcept {
    if (!scores[2].damage) {
        if (!scores[0].damage && scores[1].damage)
            return 0;
        if (scores[0].damage && !scores[1].damage)
            return 1;
    }
    std::int32_t selected = 0;
    for (std::int32_t i = 1; i < 3; ++i)
        if (scores[i].damage < scores[selected].damage ||
            (scores[i].damage == scores[selected].damage &&
             scores[i].total_distance < scores[selected].total_distance))
            selected = i;
    return selected;
}
std::int32_t score_freestanding_ray(BallisticEngine& engine, BallisticQuery& query,
                                    FreestandingScore& score, const BallisticVector& start,
                                    const BallisticVector& target) {
    BallisticFilter filter{nullptr,     0,    0x0600400Bu,
                           0x0600400Bu, 1.0f, BallisticFilterKind::reject_entities};
    FreestandingFilterEngine filtered_engine(engine);
    query.trace.fraction = 0.0f;
    trace_ballistic_bullet(filtered_engine, 0, start, target, filter, query);
    const auto damage = _mm_cvttss_si32(_mm_set_ss(query.damage));
    if (damage > 0)
        score.damage = add(score.damage, damage);
    const auto travelled = length(subtract(query.trace.end, start));
    const auto requested = length(subtract(target, start));
    auto distance = (travelled / requested) * 100.0f / 25.0f;
    distance = round_movement_step(distance) * 25.0f + 0.5f;
    const auto quantized = _mm_cvttss_si32(_mm_set_ss(distance));
    score.total_distance = add(score.total_distance, quantized);
    score.maximum_distance = std::max(score.maximum_distance, quantized);
    return damage;
}
bool find_freestanding_direction(BallisticEngine& engine, const BallisticVector& start,
                                 const BallisticVector& target, BallisticVector& direction,
                                 std::int32_t* total_damage, FreestandingWorkspace& work) {
    const auto dx = target[0] - start[0], dy = target[1] - start[1];
    const auto reciprocal =
        1.0f / (std::sqrt((dx * dx + dy * dy) + 0.0f) + std::numeric_limits<float>::epsilon());
    const BallisticVector forward{reciprocal * dx, reciprocal * dy, reciprocal * 0.0f};
    BallisticVector side;
    if (forward[0] == 0.0f && forward[1] == 0.0f)
        side = {0, -1, 0};
    else {
        const auto common = forward[2] * 0.0f;
        side = {forward[1] - common, common - forward[0], forward[0] * 0.0f - forward[1] * 0.0f};
        const auto inverse = 1.0f / (length(side) + std::numeric_limits<float>::epsilon());
        for (auto& value : side)
            value *= inverse;
    }
    auto& query = work.query;
    query.allow_penetration = 1;
    query.weapon_id = 0;
    query.penetration = 2.5f;
    query.range = 8192.0f;
    query.range_modifier = std::bit_cast<float>(0x3F7AE148u);
    query.armor_ratio = std::bit_cast<float>(0x3FD33333u);
    query.base_damage = 80.0f;
    query.reserved = 1;
    work.scores = {};
    work.scores[0].direction = side;
    work.scores[1].direction = {neg(side[0]), neg(side[1]), neg(side[2])};
    work.scores[2].direction = {neg(forward[0]), neg(forward[1]), neg(forward[2])};
    build_freestanding_rays(work.rays, work.scores, start, target, side);
    for (std::size_t i = 0; i < work.rays.size(); ++i) {
        auto ray = work.rays[i];
        auto& score = work.scores[i % 3];
        if (engine.point_contents(ray.start, 1, true) & 1u) {
            if (i < 6 || i % 3 == 2 || score.total_distance == 0)
                continue;
            ray.start = advance(start, score.direction, 10.0f);
        }
        score_freestanding_ray(engine, query, score, ray.start, ray.end);
    }
    if (total_damage) {
        *total_damage = 0;
        for (const auto& score : work.scores)
            *total_damage = add(*total_damage, score.damage);
    }
    if (!freestanding_difference(work.scores))
        return false;
    const auto selected = select_freestanding_side(work.scores);
    if (selected == -1)
        return false;
    direction = work.scores[selected].direction;
    return true;
}
bool apply_freestanding(const FreestandingSettings& settings, const FreestandingFrame& frame,
                        const FreestandingContext& context, FreestandingState& state, float& yaw,
                        BallisticEngine& engine, FreestandingWorkspace& workspace) {
    if (!settings.enabled || !hotkey_active(settings.hotkey, context.input))
        return false;
    if (!(state.flags_130 & 8u)) {
        if (context.target_handle == -1 ||
            !context.entities.by_handle(context.entities.object, context.target_handle))
            return false;
        auto start = frame.origin_10c;
        const auto absolute38 =
            std::bit_cast<float>(std::bit_cast<std::uint32_t>(frame.scalar_38) & 0x7FFFFFFFu);
        if (!(frame.scalar_c0 > 2.0f) && absolute38 == 0.0f && frame.forward_move > 0.0f &&
            frame.forward_move < 10.0f)
            start = frame.origin_168;
        bool standing = (context.flags & 0x40u) != 0;
        if (!standing)
            standing = hotkey_active(context.height_hotkey, context.input) ||
                       (context.flags & 0x100u) != 0;
        start[2] += standing ? 68.0f : frame.height_164 + 4.0f;
        auto target = context.target;
        target[2] += 64.0f;
        BallisticVector selected{};
        std::int32_t total_damage = 0;
        if (find_freestanding_direction(engine, start, target, selected, &total_damage, workspace))
            state.yaw_1a8 = planar_movement_yaw({selected[0], selected[1], selected[2]});
        else if (total_damage > 0) {
            state.value_1ac = 0;
            state.yaw_1a8 = std::numeric_limits<float>::max();
        }
    }
    if (state.yaw_1a8 == std::numeric_limits<float>::max())
        return false;
    yaw = state.yaw_1a8;
    return true;
}
} // namespace skeet::anti_aim
