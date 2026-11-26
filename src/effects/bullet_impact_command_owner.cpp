#include "skeet/effects/bullet_impact_command_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/esp/entity_overlay.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>

namespace skeet::effects {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool game_rule_override(const BulletImpactCommandBindings& bindings) {
    auto** rules = bindings.game_rules;
    return rules && *rules && read<std::uint8_t>(*rules, bindings.game_rule_override) != 0;
}
bool shotgun_patterns_enabled(const void* variable) {
    for (;;) {
        const auto* parent = read<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return (read<std::uint32_t>(variable, 0x30) ^
            std::uint32_t(reinterpret_cast<std::uintptr_t>(variable))) != 0;
}
float negate(float value) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
struct ImpactBasis {
    RageVector forward, right, up;
};
ImpactBasis impact_basis(const std::array<float, 3>& angles, const RageTrigonometry& trig) {
    //43366128, with its actual scalar CRT boundaries and scalar operation order.
    const auto pitch = angles[0] * 0.01745329238474369f;
    const auto sp = trig.sine(pitch), cp = trig.cosine(pitch);
    const auto yaw = angles[1] * 0.01745329238474369f;
    const auto sy = trig.sine(yaw), cy = trig.cosine(yaw);
    const auto roll = angles[2] * 0.01745329238474369f;
    const auto sr = trig.sine(roll), cr = trig.cosine(roll);
    const auto minus_cr = cr * -1.0f, sr_sp = sr * sp, cr_sp = cr * sp;
    return {{cy * cp, sy * cp, negate(sp)},
            {minus_cr * negate(sy) - sr_sp * cy, minus_cr * cy - sr_sp * sy, (sr * -1.0f) * cp},
            {cr_sp * cy - negate(sy) * sr, cr_sp * sy - sr * cy, cr * cp}};
}
struct ImpactTraceFilter {
    void* const* vtable;
    std::array<std::uint32_t, 5> fields_04{};
    std::uint32_t mask = 0x4600400Bu, exit_mask = 0x4600400Bu;
    float minimum_damage = 1.0f;
    BallisticFilter binding() noexcept {
        return {this, 0, mask, exit_mask, minimum_damage, BallisticFilterKind::supplied};
    }
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(ImpactTraceFilter) == 0x24);
static_assert(offsetof(ImpactTraceFilter, mask) == 0x18);
#endif
}

void process_bullet_impacts(EffectsCommandOwner& owner, const client::CommandFrame& frame,
                            void* player, const client::Command& command) {
    auto& settings = owner.world.settings;
    const auto& bindings = owner.bindings;
    if (!settings.bullet_impacts.value || owner.world.suspended)
        return;
    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    if (!weapon || !frame.weapon_data_address || game_rule_override(bindings))
        return;
    // The classification consumes the live item definition, not frame+D8.
    if (!esp::ammunition_weapon(read<std::uint16_t>(weapon, bindings.weapon_definition)))
        return;
    if (!(frame.flags & 0x100000u) && (!(frame.flags & 0x400u) || !(command.movement.buttons & 1u)))
        return;

    auto angles = command.angles;
    const auto& recoil = frame.recoil_angles_9c;
    const auto recoil_squared =
        (recoil[0] * recoil[0] + recoil[1] * recoil[1]) + recoil[2] * recoil[2];
    if (recoil_squared != 0.0f)
        for (std::size_t i = 0; i != 3; ++i)
            angles[i] = recoil[i] + angles[i];
    const auto basis = impact_basis(angles, bindings.trigonometry);
    auto direction = basis.forward;
    if (frame.flags & 0x4000u) {
        const rage::SpreadParameters parameters{std::uint32_t(frame.weapon_id_d8),
                                                std::bit_cast<std::int32_t>(frame.field_b0),
                                                frame.spread_d0,
                                                frame.inaccuracy_c4,
                                                frame.weapon_field_100,
                                                frame.pellet_count_19c,
                                                shotgun_patterns_enabled(bindings.shotgun_patterns),
                                                frame.weapon_kind_124};
        const auto sample = rage::generate_spread_sample(
            parameters, std::uint8_t(command.random_seed), bindings.spread_tables, bindings.random,
            bindings.shotgun_pattern, bindings.trigonometry);
        for (std::size_t i = 0; i != 3; ++i)
            direction[i] =
                (basis.right[i] * sample.first + basis.forward[i]) + basis.up[i] * sample.second;
        const auto squared = (direction[0] * direction[0] + direction[1] * direction[1]) +
                             direction[2] * direction[2];
        const auto root = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(squared)));
        const auto inverse = 1.0f / (root + 0x1p-23f);
        for (auto& value : direction)
            value *= inverse;
    }
    // Reload frame+C after the spread provider, as4339EE21 does. Genuine
    // weapon getters run here, before constructing/consuming the trace filter.
    auto query =
        rage::prepare_target_bullet(reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address)),
                                    bindings.weapon_definition, true);
    ImpactTraceFilter native_filter{bindings.trace_filter_vtable};
    const auto filter = native_filter.binding();
    rage::trace_target_direction(bindings.ballistics,
                                 BallisticEntity(reinterpret_cast<std::uintptr_t>(player)),
                                 frame.origin_dc, direction, filter, query);
    for (std::int32_t i = 0; i < std::bit_cast<std::int32_t>(query.point_count); ++i) {
        const RageVector minimum{-1.0f, -1.0f, -1.0f}, maximum{1.0f, 1.0f, 1.0f}, rotation{};
        const auto duration =
            _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(settings.impact_duration.value)));
        engine::invoke<void>(bindings.debug_overlay, 1, &query.points[std::size_t(i)], &minimum,
                             &maximum, &rotation, std::int32_t(255), std::int32_t(0),
                             std::int32_t(0), std::int32_t(127), duration);
    }
}
void update_effects_entities(EffectsCommandOwner& owner, void* player, std::uintptr_t incoming) {
    update_entity_render_state_packet(owner.camera, owner.players, owner.entities, player,
                                      reinterpret_cast<const void*>(incoming), owner.world.settings,
                                      owner.models.settings.weapons.enabled.value);
}
} // namespace skeet::effects
