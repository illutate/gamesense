#include "skeet/client/command_capture.hpp"
#include "skeet/client/startup_reset.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::client {
namespace {
template <class T>
[[gnu::always_inline]] inline T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
[[gnu::always_inline]] inline float add(float first, float second) noexcept {
    __asm__ __volatile__("addss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
[[gnu::always_inline]] inline float subtract(float first, float second) noexcept {
    __asm__ __volatile__("subss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
[[gnu::always_inline]] inline float multiply(float first, float second) noexcept {
    __asm__ __volatile__("mulss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
[[gnu::always_inline]] inline float divide(float first, float second) noexcept {
    __asm__ __volatile__("divss %1, %0" : "+x"(first) : "x"(second));
    return first;
}
std::uint32_t address(const void* value) noexcept {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(value));
}
void* terminal_convar(void* object) noexcept {
    for (;;) {
        auto* parent = read<void*>(object, 0x1C);
        if (object == parent)
            break;
        object = parent;
    }
    return object;
}
std::uint32_t integer_convar(void* object) noexcept {
    object = terminal_convar(object);
    return read<std::uint32_t>(object, 0x30) ^ address(object);
}
// Complete433E3CFB. Mode0 additionally requires the secondary ConVar to be0;
// modes1 and6 bypass that secondary test.
bool randomized_angle_limit(const CommandCaptureContext& context) noexcept {
    if (*context.angle_override)
        return true;
    if (!command_roll_blocked(context.angle_environment))
        return false;
    const auto& c = context.convars;
    if (c.optional_angle_override.get() && integer_convar(c.optional_angle_override.get()))
        return true;
    const auto mode = integer_convar(c.angle_mode.get());
    const auto* secondary = terminal_convar(c.angle_secondary.get());
    return mode == 0 ? read<std::uint32_t>(secondary, 0x30) == address(secondary)
                     : (mode == 1 || mode == 6);
}
[[gnu::always_inline]] inline float
interpolation_time(const CommandCaptureContext& context) noexcept {
    const auto& c = context.convars;
    const auto rate =
        std::bit_cast<float>(lagcomp::read_prediction_convar(c.interpolation_rate.get()));
    const auto minimum =
        std::bit_cast<float>(lagcomp::read_prediction_convar(c.interpolation_minimum.get()));
    const auto ratio =
        std::bit_cast<float>(lagcomp::read_prediction_convar(c.interpolation_ratio.get()));
    return maximum_number(*context.interpolation_floor,
                          maximum_number(minimum, divide(ratio, rate)));
}
std::array<float, 3> view_forward(const std::array<float, 3>& angles,
                                  const RageTrigonometry& trig) {
    constexpr float radians = 0.01745329238474369f;
    const auto yaw = multiply(angles[1], radians);
    const auto sy = trig.sine(yaw), cy = trig.cosine(yaw);
    const auto pitch = multiply(angles[0], radians);
    const auto sp = trig.sine(pitch), cp = trig.cosine(pitch);
    return {multiply(cp, cy), multiply(cp, sy),
            std::bit_cast<float>(std::bit_cast<std::uint32_t>(sp) ^ 0x80000000u)};
}
}

bool command_target_valid(const CommandTargetValidation& context) {
    auto& state = context.target;
    if (*context.sequence == state.sequence)
        return true;
    float current = 0;
    if (context.clock_owner && *context.clock_owner)
        current = read<float>(*context.clock_owner, context.clock_offset);
    if (current != state.timestamp || state.handle == -1)
        return false;
    auto* target = engine::invoke<void*>(context.entity_list, 4, state.handle);
    if (!target || !engine::invoke<bool>(target, 158) ||
        read<std::uint8_t>(target, context.rejection_byte))
        return false;
    std::array<float, 3> origin;
    if (!engine::invoke<bool>(static_cast<std::byte*>(target) + 8, 9))
        origin = read<std::array<float, 3>>(target, context.network_origin);
    else {
        if (context.dormant_owner && *context.dormant_owner) {
            const auto index = state.handle == -1 ? 0x1FFFu : std::uint16_t(state.handle);
            auto* owner = *context.dormant_owner;
            if (!read<std::uint8_t>(owner, context.dormant_present + index) ||
                !read<std::uint8_t>(owner, context.dormant_valid + index))
                return false;
        }
        const auto index = state.handle == -1 ? 0x1FFFu : std::uint16_t(state.handle);
        const auto* cached = context.players.get(index);
        if (cached->geometry.alpha == 0.0f)
            return false;
        const auto& p = cached->geometry.origin;
        origin = {p.x, p.y, p.z};
    }
    const auto y = subtract(origin[1], state.position[1]);
    const auto x = subtract(origin[0], state.position[0]);
    const auto z = subtract(origin[2], state.position[2]);
    const auto square = add(add(multiply(y, y), multiply(x, x)), multiply(z, z));
    // COMISS/JBE also accepts unordered results in the native helper.
    return !(square > 122500.0f);
}

CommandCapturedState capture_initial_command_frame(CommandFrame& frame, Command& command,
                                                   std::uint8_t& send_packet,
                                                   std::uint32_t& global_flags, float sample_time,
                                                   const std::array<float, 3>& saved_angles,
                                                   std::vector<Matrix3x4>& bone_storage,
                                                   const CommandCaptureContext& context) {
    const auto& physical = context.physical;
    auto* player = physical.player;
    const auto& offsets = context.offsets;
    const auto& common = physical.offsets.common;
    frame.flags = frame.secondary_flags = 0;
    if (command_angles_unrestricted(context.angle_environment))
        frame.flags |= 8u;
    auto* const* rules = context.angle_environment.shared_player;
    if (rules && *rules && read<std::uint8_t>(*rules, offsets.rules_guard))
        frame.flags |= 0x200000u;
    if ((read<std::uint32_t>(player, offsets.mouse.player_flags) & 8u) ||
        read<std::uint8_t>(player, offsets.mouse.player_state_043fa575) !=
            read<std::uint8_t>(player, offsets.mouse.player_state_334b460c))
        frame.flags |= 0x200u;
    if (integer_convar(context.convars.target_selector.get()))
        frame.flags |= 0x4000u;
    if (hotkey_active(context.movement_key, context.input) || (global_flags & 0x100u)) {
        frame.flags |= 0x2000u;
        if (read<std::uint32_t>(player, common.player_flags) & 1u)
            frame.field_18c = std::uint32_t(frame.choke_limit_fc) - std::uint32_t(frame.choked_15c);
    }
    if (randomized_angle_limit(context)) {
        frame.flags |= 0x40u;
        const auto magnitude =
            command.number < 0 ? 0u - std::uint32_t(command.number) : std::uint32_t(command.number);
        network::ShuffleRandom random{std::bit_cast<std::int32_t>(0u - magnitude), 0, {}};
        const auto upper =
            _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(*context.maximum_angle)));
        const auto value = network::next_shuffle_random(random);
        // Native converts the scaled double to float before its upper clamp.
        auto unit = static_cast<float>(static_cast<double>(value) * 4.656612875245797e-10);
        if (static_cast<double>(unit) > 0.99999988)
            unit = 0.9999998807907104f;
        frame.maximum_angle_step_1e8 = subtract(upper, add(unit, 0.0f));
    }
    const auto latency = network::average_latency_ticks(context.latency);
    write(&frame, 0x14C, latency);
    const auto& timing = context.timing;
    const auto tick = network::adjusted_command_tick(
        timing.base_tick, timing.have_ack, timing.acknowledged, timing.have_sent, timing.sent);
    const auto command_tick = std::uint32_t(latency) + std::uint32_t(tick);
    frame.field_d4 = command_tick;
    write(&frame, 0x144, command_tick + integer_convar(context.convars.command_tick_offset.get()));
    write(&frame, 0x198, interpolation_time(context));
    engine::invoke<void>(context.engine_client, 18,
                         static_cast<void*>(reinterpret_cast<std::byte*>(&frame) + 0xB4));
    write(&frame, 0x1A8,
          view_forward(read<std::array<float, 3>>(&frame, 0xB4), physical.trigonometry));
    frame.command_number_ac = command.number;
    frame.angles_118 = command.angles;
    frame.input_side_80 = command.movement.side_move;
    frame.input_forward_78 = command.movement.forward_move;
    write(&frame, 0xF8, read<std::uint32_t>(player, common.player_flags));
    const auto tick_base = read<std::int32_t>(player, offsets.tick_base);
    write(&frame, 0x88, tick_base);
    frame.tick_base_c8 = tick_base;
    const auto duck = read<float>(player, common.duck_amount);
    write(&frame, 0x1A4, duck);
    frame.duck_amount_98 = duck;
    write(&frame, 0x8C, read<std::array<float, 3>>(player, offsets.vector_48442721));
    const auto vy = read<float>(player, common.velocity + 4),
               vx = read<float>(player, common.velocity);
    const auto planar_speed =
        _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(add(multiply(vx, vx), multiply(vy, vy)))));
    frame.previous_speed_190 = planar_speed;
    frame.origin_168 = *engine::invoke<const std::array<float, 3>*>(player, 10);
    frame.previous_velocity_180 = read<std::array<float, 3>>(player, common.velocity);
    const auto handle = read<std::uint32_t>(player, physical.offsets.active_weapon);
    auto* weapon =
        handle == UINT32_MAX ? nullptr : engine::invoke<void*>(physical.entity_list, 4, handle);
    frame.weapon_address = address(weapon);
    if (weapon) {
        write(&frame, 0x194, read<float>(weapon, common.weapon_field_6777c9ce));
        write(&frame, 0xF4, read<float>(weapon, offsets.weapon_field_d2563274));
    }
    frame.flags &= ~0x400000u;
    write(&frame, 0x1B4, sample_time);
    capture_command_physical_frame(frame, command, physical, CommandCapturePass::initial);
    frame.field_108 = frame.field_c4;
    frame.field_1ec = frame.field_1f0 = 0;
    frame.field_1d0 = read<std::uint32_t>(player, offsets.team);

    auto* cache = static_cast<std::byte*>(player) + common.bone_cache;
    const auto count = read<std::int32_t>(cache, 0xC);
    if (bone_storage.size() != std::size_t(count))
        bone_storage.resize(std::size_t(count));
    if (!bone_storage.empty())
        std::memcpy(bone_storage.data(), read<const Matrix3x4*>(cache, 0),
                    bone_storage.size() * sizeof(Matrix3x4));
    auto& target = context.target_validation.target;
    if (target.handle != -1 && engine::invoke<void*>(physical.entity_list, 4, target.handle) &&
        !command_target_valid(context.target_validation))
        reset_target_state(
            {target.handle, target.fields_18[0], target.fields_18[1], target.fields_18[3]});
    frame.angles_118 = saved_angles;
    enforce_command_choke_limit(send_packet, frame.choked_15c, frame.choke_limit_fc);
    return {{bone_storage.data(), count}, planar_speed, frame.scalar_c0};
}
} // namespace skeet::client
