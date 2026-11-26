#include "skeet/client/command_recapture.hpp"
#include "skeet/client/command_angle_retention.hpp"
#include "skeet/client/command_eye_adjustment.hpp"
#include "skeet/rage/targeting_commit.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::client {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
[[gnu::always_inline]] inline float multiply(float first, float second) noexcept {
    __asm__ __volatile__("mulss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
[[gnu::always_inline]] inline float add(float first, float second) noexcept {
    __asm__ __volatile__("addss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
std::uint32_t address(const void* value) noexcept {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(value));
}
void* active_weapon(const CommandRecaptureContext& context) {
    const auto handle = read<std::uint32_t>(context.player, context.offsets.active_weapon);
    return handle == UINT32_MAX ? nullptr : engine::invoke<void*>(context.entity_list, 4, handle);
}
}
std::array<float, 3> capture_command_recoil(void* player, std::ptrdiff_t offset, void* convar) {
    const auto scale = std::bit_cast<float>(lagcomp::read_prediction_convar(convar));
    const auto recoil = read<std::array<float, 3>>(player, offset);
    return {multiply(recoil[0], scale), multiply(recoil[1], scale), multiply(recoil[2], scale)};
}
void update_predicted_command_weapon(const CommandRecaptureContext& context) {
    if (auto* weapon = active_weapon(context)) {
        if (engine::invoke<std::int32_t>(weapon, 455) == 9)
            return;
        const auto& o = context.fire.offsets;
        const auto definition = read<std::uint16_t>(weapon, o.weapon_definition);
        bool update = definition != 49;
        if (update && (definition == 4 || definition == 10) &&
            read<std::int32_t>(weapon, o.burst_remaining) > 0 &&
            *context.fire.current_time >= read<float>(weapon, o.next_burst) &&
            read<std::int32_t>(weapon, o.ammunition) > 0)
            update = false;
        if (update)
            engine::invoke<void>(weapon, 484);
    }
    const auto scale =
        std::bit_cast<float>(lagcomp::read_prediction_convar(context.recoil_scale_convar.get()));
    write(context.player, context.offsets.field_66146802,
          multiply(scale, read<float>(context.player, context.offsets.recoil_angles)));
}
void capture_predicted_command_frame(CommandFrame& frame, Command& command,
                                     const CommandRecaptureContext& context) {
    frame.flags |= 1;
    capture_command_physical_frame(frame, command, context, CommandCapturePass::predicted);
}
void capture_command_physical_frame(CommandFrame& frame, Command& command,
                                    const CommandRecaptureContext& context,
                                    CommandCapturePass pass) {
    const auto& o = context.offsets;
    frame.duck_amount_98 = read<float>(context.player, o.common.duck_amount);
    const auto player_flags = read<std::uint32_t>(context.player, o.common.player_flags);
    frame.player_flags_7c = player_flags;
    frame.flags ^= ((player_flags << 18) ^ frame.flags) & 0x40000u;
    frame.state_104 = read<std::uint8_t>(context.player, o.byte_7501cb91);
    frame.origin_10c = *engine::invoke<const std::array<float, 3>*>(context.player, 10);
    frame.height_164 = read<float>(context.player, o.view_offset + 8);
    frame.velocity_150 = read<std::array<float, 3>>(context.player, o.common.velocity);
    const auto& v = frame.velocity_150;
    const auto square = add(add(multiply(v[0], v[0]), multiply(v[1], v[1])), multiply(v[2], v[2]));
    frame.scalar_c0 = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(square)));
    frame.origin_dc = frame.origin_10c;
    frame.origin_dc[2] = add(frame.origin_dc[2], frame.height_164);
    frame.view_height_84 = frame.height_164;
    frame.field_1e4 = 0x7F7FFFFFu;
    auto* weapon = active_weapon(context);
    frame.weapon_address = address(weapon);
    if (weapon) {
        frame.weapon_id_d8 = read<std::uint16_t>(weapon, context.fire.offsets.weapon_definition);
        const auto* data = engine::invoke<const void*>(weapon, 461);
        frame.weapon_data_address = address(data);
        frame.weapon_kind_124 = std::uint8_t(engine::invoke<std::int32_t>(weapon, 455));
        frame.field_b0 = read<std::uint32_t>(weapon, o.weapon_field_572aa59f);
        frame.field_100 = read<std::uint32_t>(weapon, o.common.weapon_field_6777c9ce);
        if (frame.weapon_id_d8 == 64) {
            frame.flags ^= ((frame.flags >> 3) ^ frame.flags) & 0x800u;
            frame.flags ^= (~(frame.flags << 4) ^ frame.flags) & 0x8000u;
            frame.postpone_fire_1e4 = read<float>(weapon, context.fire.offsets.postpone_fire);
        }
        const auto ready =
            skeet::weapon::evaluate_fire_state(context.fire, context.player, weapon, command);
        if (ready.primary_ready)
            frame.flags |= 0x400u;
        if (ready.secondary_ready)
            frame.flags |= 2u;
        if (ready.burst_ready) {
            frame.flags |= 0x100000u;
            command.movement.buttons |= 1u;
        }
        const auto spread = engine::invoke<float>(weapon, 453);
        frame.spread_d0 = spread;
        frame.inaccuracy_c4 = engine::invoke<float>(weapon, 483);
        if (data) {
            frame.pellet_count_19c = read<std::int32_t>(data, 0xFC);
            const auto bullet =
                rage::prepare_target_bullet(weapon, context.fire.offsets.weapon_definition, false);
            frame.bullet_128.weapon_id = std::uint16_t(bullet.weapon_id);
            frame.bullet_128.penetration = bullet.penetration;
            frame.bullet_128.range = bullet.range;
            frame.bullet_128.range_modifier = bullet.range_modifier;
            frame.bullet_128.armor_ratio = bullet.armor_ratio;
            frame.bullet_128.damage = bullet.base_damage;
            frame.bullet_128.pellet_count = bullet.reserved;
        } else {
            frame.pellet_count_19c = 1;
            std::memset(&frame.bullet_128, 0, sizeof frame.bullet_128);
        }
        if (frame.weapon_id_d8 == 64)
            frame.field_160 = (read<std::uint32_t>(context.player, o.common.player_flags) & 6u)
                                  ? 0x3B252696u
                                  : 0x3BC1FC90u;
        else {
            frame.adjusted_spread_160 = spread;
            bool alternate = false;
            if (engine::invoke<std::int32_t>(weapon, 443))
                alternate = *context.secondary_override != 0 ||
                            engine::invoke<std::int32_t>(weapon, 462) != 0;
            const auto adjustment =
                (read<std::uint32_t>(context.player, o.common.player_flags) & 6u)
                    ? engine::invoke<float>(weapon, 436, alternate)
                    : engine::invoke<float>(weapon, 435, alternate);
            frame.adjusted_spread_160 = add(adjustment, frame.adjusted_spread_160);
        }
    } else {
        frame.field_100 = 0;
        frame.spread_d0 = 0.0f;
        frame.field_c4 = 0;
        frame.flags &= ~0x402u;
        frame.weapon_id_d8 = 0;
        frame.weapon_kind_124 = 0;
        frame.weapon_data_address = 0;
        frame.pellet_count_19c = 0;
        std::memset(&frame.bullet_128, 0, sizeof frame.bullet_128);
    }
    const auto recoil =
        capture_command_recoil(context.player, o.recoil_angles, context.recoil_scale_convar.get());
    frame.recoil_angles_9c = recoil;
    constexpr float radians = 0.01745329238474369f;
    const auto pitch_sum = pass == CommandCapturePass::initial ? add(recoil[0], command.angles[0])
                                                               : add(command.angles[0], recoil[0]);
    const auto yaw = multiply(add(command.angles[1], recoil[1]), radians);
    const auto sy = context.trigonometry.sine(yaw), cy = context.trigonometry.cosine(yaw);
    const auto pitch = multiply(pitch_sum, radians);
    const auto sp = context.trigonometry.sine(pitch), cp = context.trigonometry.cosine(pitch);
    const std::array<float, 3> forward{
        multiply(cp, cy), multiply(cp, sy),
        std::bit_cast<float>(std::bit_cast<std::uint32_t>(sp) ^ 0x80000000u)};
    frame.forward_174 = forward;
    frame.player_field_a8 = read<float>(context.player, o.field_5ae32976);
    frame.hull_minimum_1b8 = *engine::invoke<const std::array<float, 3>*>(context.player, 76);
    frame.hull_maximum_1c4 = *engine::invoke<const std::array<float, 3>*>(context.player, 77);
}
template <class Eye>
void recapture_prediction(CommandFrame& frame, Command& command,
                          prediction::CommandPrediction& active, CommandCopyOverrideScope& outer,
                          const CommandRecaptureContext& context, const Eye& eye) {
    update_predicted_command_weapon(context);
    active.restore();
    capture_predicted_command_frame(frame, command, context);
    adjust_command_eye(frame, context.player, command, eye);
    context.published_origin = *engine::invoke<const std::array<float, 3>*>(context.player, 10);
    outer.restore();
}
void recapture_command_prediction(CommandFrame& frame, Command& command,
                                  prediction::CommandPrediction& active,
                                  CommandCopyOverrideScope& outer,
                                  const CommandRecaptureContext& context,
                                  const CommandEyeAdjustmentContext& eye) {
    recapture_prediction(frame, command, active, outer, context, eye);
}
void recapture_command_prediction(CommandFrame& frame, Command& command,
                                  prediction::CommandPrediction& active,
                                  CommandCopyOverrideScope& outer,
                                  const CommandRecaptureContext& context,
                                  const CommandEyeAdjustmentSourceContext& eye) {
    recapture_prediction(frame, command, active, outer, context, eye);
}
} // namespace skeet::client
