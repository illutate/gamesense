#include "skeet/legit/trigger_trace.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>
#include <stdexcept>

namespace skeet::legit {
namespace {
constexpr std::array<std::uint8_t, 128> seeds{
    2,   3,   4,   10,  13,  15,  27,  29, 37, 38,  41,  46,  47,  49,  50,  52,  56,  57,  58,
    62,  66,  68,  70,  72,  76,  83,  87, 97, 108, 109, 110, 111, 113, 121, 122, 125, 135, 1,
    5,   6,   8,   17,  25,  26,  28,  31, 32, 36,  39,  44,  51,  61,  67,  71,  74,  77,  78,
    79,  80,  81,  84,  89,  90,  91,  92, 96, 98,  101, 102, 104, 107, 114, 115, 119, 9,   11,
    16,  19,  20,  22,  30,  48,  54,  59, 60, 65,  69,  73,  75,  82,  85,  86,  88,  94,  99,
    103, 105, 112, 116, 117, 118, 124, 0,  7,  12,  14,  18,  21,  23,  24,  33,  34,  35,  40,
    42,  43,  45,  53,  55,  63,  64,  93, 95, 100, 106, 120, 133, 137};
bool accepts(const ProfileState& settings, const BallisticQuery& query,
             const TriggerTraceInput& input, TriggerTraceBindings bindings) {
    if (input.live_targets)
        return trigger_hit_allowed_live(
            settings,
            {query.damage, std::uint32_t(query.trace.hitgroup),
             reinterpret_cast<void*>(std::uintptr_t(query.trace.entity))},
            input.live_targets);
    if (_mm_cvttss_si32(_mm_set_ss(query.damage)) < settings.minimum_damage ||
        !(settings.trigger_hitgroups & (1u << (std::uint32_t(query.trace.hitgroup) & 31u))))
        return false;
    if (settings.trigger_reaction != 0.0f && query.trace.entity) {
        if (!bindings.entity_index)
            throw std::invalid_argument(
                "Trigger reaction time requires the entity networkable index");
        const auto index = bindings.entity_index(bindings.context, query.trace.entity);
        if (settings.trigger_reaction > input.target_times[std::uint32_t(index) & 63u])
            return false;
    }
    return true;
}
}
bool evaluate_trigger_samples(BallisticEngine& engine, const ProfileState& settings,
                              const TriggerTraceInput& input, const BallisticFilter& filter,
                              BallisticQuery& query, const rage::SpreadTables& tables,
                              const rage::SpreadRandom& random, const rage::ShotgunPattern& pattern,
                              const RageTrigonometry& trigonometry, TriggerTraceBindings bindings) {
    const auto shifted = std::uint32_t(settings.minimum_hit_chance) << 7;
    auto required = std::bit_cast<std::int32_t>(shifted) / 100;
    if (required < 1)
        required = 1;
    auto parameters = input.spread;
    if (parameters.weapon_id == 64) {
        parameters.spread = 0.00052f;
        parameters.inaccuracy = (input.weapon_flags_7c & 6u) ? 0.003f : 0.0077f;
        parameters.fire_mode = 0;
    }
    parameters.pellet_count = 1;
    std::int32_t hits = 0;
    for (std::int32_t i = 0; i != 128; ++i) {
        const auto sample = rage::generate_spread_sample(parameters, seeds[std::size_t(i)], tables,
                                                         random, pattern, trigonometry);
        Vector direction;
        for (std::size_t axis = 0; axis != 3; ++axis) {
            const auto first = sample.first * input.basis.first[axis] + input.basis.center[axis];
            direction[axis] = first + sample.second * input.basis.second[axis];
        }
        const auto length = std::sqrt((direction[0] * direction[0] + direction[1] * direction[1]) +
                                      direction[2] * direction[2]);
        const auto inverse = 1.0f / (length + 1.1920928955078125e-7f);
        for (auto& component : direction)
            component *= inverse;
        if (rage::trace_target_direction(engine, input.source, input.origin, direction, filter,
                                         query) &&
            accepts(settings, query, input, bindings)) {
            ++hits;
            if (hits >= required)
                return true;
        }
        const auto possible = hits - i + 128;
        if (possible < required)
            return false;
    }
    return hits >= required;
}
bool trace_trigger(BallisticEngine& engine, const ProfileState& settings,
                   const TriggerTraceInput& input, const BallisticFilter& filter,
                   BallisticQuery& query, const rage::SpreadTables& tables,
                   const rage::SpreadRandom& random, const rage::ShotgunPattern& pattern,
                   const RageTrigonometry& trigonometry, TriggerTraceBindings bindings) {
    if (!rage::trace_target_direction(engine, input.source, input.origin, input.basis.center,
                                      filter, query))
        return false;
    if (!(settings.flags & 0x10u) && bindings.through_smoke &&
        bindings.through_smoke(bindings.context, input.origin, query.trace.end))
        return false;
    if (settings.minimum_hit_chance == 0)
        return true;
    return evaluate_trigger_samples(engine, settings, input, filter, query, tables, random, pattern,
                                    trigonometry, bindings);
}
} // namespace skeet::legit
