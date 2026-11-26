#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include "action_detail.hpp"
#include "skeet/engine/interface_call.hpp"
#include "../rage_math.hpp"
#include "../rage_point_math.hpp"

namespace skeet::rage::action_detail {
namespace {
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t minimum_damage(const client::CommandFrame& frame, void* target, std::int32_t raw,
                            std::uint32_t flags) {
    if (raw >= 100)
        return add(engine::invoke<std::int32_t>(target, 122), add(raw, -100));
    if (raw != 0) {
        const auto limit = add(engine::invoke<std::int32_t>(target, 122), 5);
        return limit < raw ? limit : raw;
    }
    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    const auto factor = engine::invoke<float>(weapon, 433, std::int32_t(0));
    const auto health = add(engine::invoke<std::int32_t>(target, 122), 1);
    float value = float(health > 10 ? health : 10) * factor;
    if (!(flags & 4U))
        value *= 1.25f;
    value = minimum_number(value, float(engine::invoke<std::int32_t>(weapon, 431)) - 1.0f);
    value = maximum_number(value, 1.0f);
    auto result = _mm_cvttss_si32(_mm_set_ss(value + 0.5f));
    const auto shots =
        rage_detail::shots_to_kill(engine::invoke<std::int32_t>(target, 122), result);
    if (shots > 1) {
        const auto quotient = engine::invoke<std::int32_t>(target, 122) / shots;
        const auto quartered = std::bit_cast<std::int32_t>(std::uint32_t(quotient) << 2);
        const auto reduced = add(quartered / 3, 1);
        if (reduced < result)
            result = reduced;
    }
    return result;
}
}
void prepare_settings(const client::CommandFrame& frame, void* target, std::uint32_t entity,
                      AcquisitionTargetSettings& settings, const ActionStateBindings& bindings) {
    const auto& b = bindings.target_preparation;
    const auto original_mode = std::bit_cast<std::int32_t>(settings.words[0]);
    if (settings.words[0]) {
        auto& player = *bindings.scan.players.get(entity);
        if (!player.history.count)
            settings.words[0] = 0;
        else {
            const auto handle = bindings.preamble.yaw_target.handle;
            const auto remembered =
                handle == -1 ? nullptr : engine::invoke<void*>(b.targeting.entity_list, 4, handle);
            if (remembered != target ||
                (bindings.state.previous_targets[(entity & 63U) >> 5] & (1U << (entity & 31U))))
                settings.words[0] = 1;
        }
    }
    settings.words[5] = std::uint32_t(minimum_damage(
        frame, target, std::bit_cast<std::int32_t>(settings.words[5]), bindings.state.flags));
    if (settings.words[4])
        settings.words[3] =
            std::bit_cast<std::uint32_t>(acquisition_point_scale(frame, target, settings, b));
    const auto target_origin = read<RageVector>(target, b.targeting.offsets.origin);
    const auto x = target_origin[0] - frame.origin_10c[0];
    const auto y = target_origin[1] - frame.origin_10c[1];
    const auto z = target_origin[2] - frame.origin_10c[2];
    const auto squared = (x * x + y * y) + z * z;
    if (squared > 4000000.0f) {
        settings.words[4] &= ~0x2F0U;
        if (settings.words[1] != 0x200U)
            settings.words[1] &= ~0x200U;
    }
    if (squared > 1000000.0f)
        settings.words[4] &= ~0x200U;
    // This hotkey is evaluated before the unconditional cache lookup.
    const auto body_key = hotkey_active(b.global.force_body_aim.value.binding, b.input);
    auto& player = *bindings.scan.players.get(entity);
    auto countdown = read<std::int32_t>(&player.native, 0xF8);
    if (countdown > 0) {
        settings.option(0xDD, 1);
        if (!(bindings.state.flags & 9U)) {
            --countdown;
            write(&player.native, 0xF8, countdown);
        }
    }
    const auto& adjustments = bindings.player_adjustments.records[entity].adjustments;
    if (adjustments.prefer_body_aim == 1)
        settings.option(0xDD, 0);
    else if (adjustments.prefer_body_aim == 2 || adjustments.prefer_body_aim == 3)
        settings.option(0xDD, 1);
    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    const auto restricted = [&] {
        return (bindings.state.flags & 0x400U) &&
               read<std::uint16_t>(weapon, b.targeting.offsets.weapon_identifier) != 31;
    };
    if ((adjustments.prefer_body_aim == 3 || body_key) && !restricted()) {
        settings.words[1] &= 0xC;
        settings.words[2] &= 0xC;
        if (!(settings.words[1] | settings.words[2]))
            settings.words[1] = 0xC;
    }
    if (adjustments.safe_point == 2) {
        settings.words[2] |= settings.words[1];
        settings.words[1] = 0;
    }
    if (!settings.words[1] && settings.words[2])
        settings.words[2] &= ~0x200U;
    const auto correction = read<float>(&player.native, 0x51C);
    if (adjustments.correction_active && b.global.prefer_safe_point.value &&
        (settings.words[1] & 0xC) == 0xC && !(correction >= 0.5f) &&
        read<std::int32_t>(target, b.targeting.offsets.team) == 2 &&
        read<float>(target, b.targeting.offsets.duck_amount) >= 0.8f &&
        (read<std::uint8_t>(target, b.targeting.offsets.movement_flags) & 1U))
        settings.words[4] &= ~8U;
    if (restricted()) {
        settings.words[1] &= 2;
        settings.words[2] &= 2;
        settings.words[4] &= 2;
        settings.option(0xDD, 0);
        settings.option(0xDF, 0);
    }
    // Visualization deliberately admits only head/stomach/leg groups, and
    // folds accuracy modes above one back to one after the normal policy work.
    settings.words[0] = std::uint32_t(original_mode > 1 ? 1 : original_mode);
    settings.words[1] &= 0xCAU;
    settings.words[2] &= 0xCAU;
    settings.words[4] &= 0xCAU;
}
std::array<RageVector, 2> origins(const client::CommandFrame& frame, void* target,
                                  const ActionStateBindings& bindings) {
    using namespace rage_point_detail;
    const auto position =
        read<RageVector>(target, bindings.target_preparation.targeting.offsets.origin);
    const RageVector delta{position[0] - frame.origin_10c[0], position[1] - frame.origin_10c[1],
                           0.0f};
    const auto axis = normalize(delta, length(delta));
    const auto side = basis_from_axis(axis).side;
    std::array<RageVector, 2> result;
    for (std::size_t i = 0; i != 3; ++i) {
        const auto offset = side[i] * 36.0f;
        result[0][i] = frame.origin_dc[i] + offset;
        result[1][i] = frame.origin_dc[i] - offset;
    }
    for (auto& end : result) {
        MovementCollisionTrace trace;
        const auto& start = frame.origin_dc;
        const auto ray = make_movement_hull_ray(
            {start[0], start[1], start[2]}, {end[0], end[1], end[2]}, {-16, -16, -18}, {16, 16, 2});
        trace_movement_world(bindings.collision, ray, 1, trace, bindings.trace_flag);
        end = {trace.end.x, trace.end.y, trace.end.z};
    }
    return result;
}
} // namespace skeet::rage::action_detail
