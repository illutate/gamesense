#include "restricted_player_command_internal.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include "skeet/movement/steering_math.hpp"
#include "skeet/prediction/observer_registry.hpp"
#include "skeet/engine/hooks.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <emmintrin.h>

namespace skeet::client::restricted_detail {
namespace {
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU);
}
float negated(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000U);
}
float unit(float value) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_setzero_ps()), _mm_set_ss(1.0f)));
}
float approach(float target, float value, float amount) noexcept {
    const auto difference = target - value;
    if (difference > amount)
        return value + amount;
    if (negated(amount) > difference)
        return value - amount;
    return target;
}
float angle_difference(float first, float second) noexcept {
    auto difference = float(std::fmod(double(first - second), 360.0));
    if (first > second) {
        if (difference >= 180.0f)
            difference -= 360.0f;
    } else if (difference <= -180.0f)
        difference += 360.0f;
    return difference;
}
float quantized_angle(float value) noexcept {
    const auto integer =
        _mm_cvttss_si32(_mm_mul_ss(_mm_set_ss(value), _mm_set_ss(182.04444885253906f)));
    return float(std::uint32_t(integer) & 0xFFFFU) * 0.0054931640625f;
}
float approach_angle(float target, float value, float amount) noexcept {
    target = quantized_angle(target);
    value = quantized_angle(value);
    const auto difference = normalize_movement_angle(target - value);
    if (difference > amount)
        return value + amount;
    if (difference < negated(amount))
        return value - amount;
    return target;
}
void advance_cycle(void* layer, float delta) noexcept {
    const auto rate = read<float>(layer, 0x28);
    if (absolute(rate) <= 0.0f)
        return;
    auto cycle = rate * delta + read<float>(layer, 0x2C);
    if (cycle >= 1.0f)
        cycle = 0.9990000128746033f;
    const auto integer = _mm_cvttss_si32(_mm_set_ss(cycle));
    cycle -= float(integer);
    if (cycle < 0.0f)
        cycle += 1.0f;
    else if (cycle > 1.0f)
        cycle -= 1.0f;
    write(layer, 0x2C, cycle);
}
void set_sequence(void* player, void* layer, std::int32_t sequence) {
    write(layer, 0x2C, std::uint32_t(0));
    write(layer, 0x20, std::uint32_t(0));
    write(layer, 0x18, sequence);
    write(layer, 0x28, engine::invoke<float>(player, 223, layer, sequence));
}
void jump_layer(RestrictedPlayerCommandOwner& owner, void* player, void* state, std::byte* layers,
                std::int32_t count, const animation::LocalAnimationBindings& helpers) {
    animation::mark_animation_modifiers(state);
    write(state, 0x10B, std::uint8_t(1));
    if (count <= 4)
        return;
    auto sequence = animation::select_symbol_weighted_sequence(player, 4, 0x3D9, helpers);
    if (sequence <= 1) {
        std::array<char, 64> text;
        ::strcpy_s(text.data(), text.size(), owner.jump_activity);
        const auto modifiers = read<std::uint8_t>(state, 0x32B);
        if (modifiers & 1U)
            ::strcat_s(text.data(), text.size(), owner.moving_modifier);
        if (modifiers & 2U)
            ::strcat_s(text.data(), text.size(), owner.crouching_modifier);
        sequence = animation::map_animation_activity(player, text.data(), helpers);
    }
    if (sequence > 1 && std::uintptr_t(layers) + 4U * 0x38U)
        set_sequence(player, layers + 4 * 0x38, sequence);
}
std::int32_t observer_tick(const RestrictedPlayerCommandBindings& b, const Command& command,
                           std::int32_t tick) {
    return b.tickbase_observers->adjust_tick(command.number, tick);
}
void* active_weapon(void* player, const RestrictedPlayerCommandBindings& b) {
    const auto handle = read<std::uint32_t>(player, b.offsets.weapon_handle);
    return handle == UINT32_MAX ? nullptr : engine::invoke<void*>(b.entity_list, 4, handle);
}
void prepare_idle_layer(RestrictedPlayerCommandOwner& owner, void* player, void* state,
                        Command& command, std::byte* layers,
                        const RestrictedPlayerCommandBindings& b) {
    auto* layer = layers + 3 * 0x38;
    const auto& h = b.helpers;
    const auto velocity = read<std::array<float, 3>>(player, b.offsets.field_36375c23);
    const auto speed =
        minimum_number(std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]), 260.0f);
    const auto delta = maximum_number(0.0f, *b.globals_current_time - read<float>(state, 0x6C));
    float maximum_speed = 260.0f;
    if (auto* weapon = active_weapon(player, b)) {
        maximum_speed = 0.0010000000474974513f;
        if (engine::invoke<float>(weapon, 442) > maximum_speed)
            maximum_speed = engine::invoke<float>(weapon, 442);
    }
    const auto walk_speed = speed / (maximum_speed * 0.5199999809265137f);
    const auto duck_speed = speed / (maximum_speed * 0.3400000035762787f);
    const auto transition = read<float>(state, 0x11C);
    const auto duck_target =
        unit(read<float>(player, b.offsets.field_3309c0cf) + read<float>(state, 0xA8));
    const auto duck = unit(approach(duck_target, read<float>(state, 0xA4), delta * 6.0f));
    const bool still = !(speed > 0.0f) && 0.0f >= read<float>(state, 0x104);
    if (!read<std::uint8_t>(state, 0x138) && still && read<std::uint8_t>(state, 0x108) &&
        !read<std::uint8_t>(state, 0x128) && !read<std::uint8_t>(state, 0x109) &&
        50.0f > read<float>(state, 0x2AC)) {
        std::array<char, 64> text;
        ::strcpy_s(text.data(), text.size(), owner.idle_activity);
        if (read<std::uint8_t>(state, 0x32B) & 2U)
            ::strcat_s(text.data(), text.size(), owner.layer_modifier);
        ::strcat_s(text.data(), text.size(), owner.idle_modifier);
        const auto sequence = animation::map_animation_activity(player, text.data(), h);
        if (sequence > 1) {
            if (std::uintptr_t(layer))
                set_sequence(player, layer, sequence);
            write(state, 0x138, std::uint8_t(1));
        }
    }
    if (h.sequence_activity) {
        const auto activity = h.sequence_activity(player, read<std::int32_t>(layer, 0x18));
        if (activity == 0x3D4 || activity == 0x3D3) {
            if (read<std::uint8_t>(state, 0x138) && duck_speed <= 0.25f) {
                if (!(delta <= 0.0f)) {
                    advance_cycle(layer, delta);
                    if (h.layer_weight_entry) {
                        const auto previous = read<float>(layer, 0x20);
                        const auto weight = unit(animation::read_local_layer_weight(state, 3, h));
                        write(layer, 0x20, weight);
                        write(layer, 0x24, (weight - previous) / delta);
                    }
                }
                write(state, 0x138,
                      std::uint8_t(read<float>(layer, 0x28) * delta + read<float>(layer, 0x2C) <
                                   0.9990000128746033f));
            } else {
                write(state, 0x138, std::uint8_t(0));
                const auto previous = read<float>(layer, 0x20);
                const auto weight = unit(approach(0.0f, previous, delta * 5.0f));
                write(layer, 0x20, weight);
                if (delta != 0.0f)
                    write(layer, 0x24, (weight - previous) / delta);
            }
        }
    }
    if (!(read<float>(state, 0xEC) <= 1.0f) || !read<std::uint8_t>(state, 0x108) ||
        read<std::uint8_t>(state, 0x128) || read<std::uint8_t>(state, 0x109) || !(delta > 0.0f))
        return;
    const auto previous_goal = read<float>(state, 0x80);
    auto goal = _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(previous_goal), _mm_set_ss(-360.0f)), _mm_set_ss(360.0f)));
    const auto eye = normalize_movement_angle(command.angles[1]);
    const auto difference = angle_difference(eye, goal);
    auto modifier =
        ((0.800000011920929f - transition * 0.30000001192092896f) - 1.0f) * unit(walk_speed) + 1.0f;
    if (duck > 0.0f)
        modifier = unit(duck_speed) * duck * (0.5f - modifier) + modifier;
    const auto maximum = read<float>(state, 0x338) * modifier;
    const auto minimum = read<float>(state, 0x334) * modifier;
    if (difference > maximum)
        goal = eye - absolute(maximum);
    else if (difference < minimum)
        goal = absolute(minimum) + eye;
    goal = normalize_movement_angle(goal);
    if (speed > 0.1f || absolute(velocity[2]) > 100.0f)
        goal = approach_angle(eye, goal, absolute((transition * 20.0f + 30.0f) * delta));
    else
        goal = approach_angle(*b.body_yaw, goal, absolute(delta * 100.0f));
    if (absolute(angle_difference(previous_goal, goal)) / delta > 120.0f) {
        const auto sequence =
            animation::select_prediction_sequence(player, 0x3D3, command.random_seed, h);
        if (sequence > 1) {
            write(layer, 0x18, sequence);
            const auto rate = engine::invoke<float>(player, 223, layer, sequence);
            write(layer, 0x2C, std::uint32_t(0));
            write(layer, 0x20, std::uint32_t(0));
            write(layer, 0x28, rate);
            write(state, 0x138, std::uint8_t(1));
        }
    }
}
void prepare_landing_tag(RestrictedPlayerCommandOwner& owner, void* player, void* state,
                         std::byte* layers, const RestrictedPlayerCommandBindings& b) {
    auto* layer = layers + 10 * 0x38;
    const auto command_address = read<std::uint32_t>(player, b.offsets.field_f58e6118);
    if (command_address && b.helpers.sequence_activity) {
        const auto* command = reinterpret_cast<const Command*>(std::uintptr_t(command_address));
        const auto number = std::uint32_t(command->number);
        if (owner.landing_tags[number % 150U] == number) {
            const auto sequence = read<std::int32_t>(layer, 0x18);
            const auto activity = b.helpers.sequence_activity(player, sequence);
            if (activity == 0x3D7 || activity == 0x3D6) {
                write(layer, 0x2C, std::uint32_t(0));
                write(layer, 0x20, std::uint32_t(0));
                write(layer, 0x28, engine::invoke<float>(player, 223, layer, sequence));
            }
        }
    }
    if (read<std::int32_t>(layer, 0x18) <= 0)
        write(layer, 0x20, std::uint32_t(0));
    else if (b.helpers.layer_weight_entry)
        write(layer, 0x20, unit(animation::read_local_layer_weight(state, 10, b.helpers)));
}
void update_post_layers(RestrictedPlayerCommandOwner& owner, void* player, void* state,
                        std::byte* layers, std::int32_t count, bool replay,
                        const RestrictedPlayerCommandBindings& b) {
    if (count > 2) {
        auto* first = layers + 0x38;
        auto* second = layers + 2 * 0x38;
        const auto duck = read<float>(state, 0xA4);
        const auto delta =
            replay ? read<float>(state, 0x74) : *b.current_time - read<float>(state, 0x6C);
        if (read<std::int32_t>(first, 0x18) <= 0)
            write(first, 0x20, std::uint32_t(0));
        if (read<std::int32_t>(second, 0x18) <= 0)
            write(second, 0x20, std::uint32_t(0));
        const auto first_weight = read<float>(first, 0x20);
        float weight;
        if (first_weight > 0.0f) {
            const char* name = "";
            if (b.helpers.sequence_name_entry) {
                const auto function = b.helpers.sequence_name_entry;
                name = function(player, read<std::int32_t>(first, 0x18));
            }
            if (name && std::strstr(name, owner.layer_modifier))
                weight = duck < 1.0f ? (1.0f - duck) * first_weight : 0.0f;
            else
                weight = duck > 0.0f ? duck * first_weight : 0.0f;
        } else {
            const auto previous = read<float>(second, 0x20);
            weight = previous > 0.0f ? approach(0.0f, previous, delta * 4.0f) : 0.0f;
        }
        write(second, 0x20, unit(weight));
        if (!(delta <= 0.0f)) {
            advance_cycle(first, delta);
            if (b.helpers.layer_weight_entry) {
                const auto previous = read<float>(first, 0x20);
                const auto current = unit(animation::read_local_layer_weight(state, 1, b.helpers));
                write(first, 0x20, current);
                write(first, 0x24, (current - previous) / delta);
            }
        }
    }
    if (count > 11) {
        auto* layer = layers + 11 * 0x38;
        if (read<std::int32_t>(layer, 0x18) <= 0)
            write(layer, 0x20, std::uint32_t(0));
        else if (read<float>(layer, 0x28) * read<float>(state, 0x74) + read<float>(layer, 0x2C) <
                 0.9990000128746033f)
            write(layer, 0x20,
                  unit(1.0f - unit((read<float>(state, 0xF4) - 0.55000001192092896f) /
                                   0.34999996423721313f)));
    }
}
}
void update_animation_step(RestrictedPlayerCommandOwner& owner, void* player, void* state,
                           Command& command, bool jump_reset, std::int32_t seed, std::int32_t count,
                           const std::array<float, 3>* replay_angles,
                           const RestrictedPlayerCommandBindings& b) {
    auto* descriptor = static_cast<std::byte*>(player) + b.offsets.animation.animation_layers;
    auto* layers = read<std::byte*>(descriptor, 0);
    const bool replay = replay_angles != nullptr;
    if (replay && jump_reset)
        jump_layer(owner, player, state, layers, count, b.helpers);
    const auto tick = observer_tick(b, command, seed);
    *b.current_time = float(tick) * *b.interval;
    if (!replay) {
        layers = read<std::byte*>(descriptor, 0);
        if (jump_reset)
            jump_layer(owner, player, state, layers, count, b.helpers);
        if (read<std::int32_t>(state, 0x70) >= *b.frame_number)
            write(state, 0x70, std::bit_cast<std::int32_t>(std::uint32_t(*b.frame_number) - 1U));
    }
    animation::apply_movement_button_state(player, command, b.helpers);
    if (replay) {
        const auto slot = std::uint32_t(command.number) % 150U;
        write(player, b.offsets.field_66146802, owner.prediction_records[slot].recoil_pitch);
    } else {
        const auto scale =
            std::bit_cast<float>(lagcomp::read_prediction_convar(b.recoil_scale_convar));
        write(player, b.offsets.field_66146802,
              scale * read<float>(player, b.offsets.field_1bae1168));
    }
    write(player, b.offsets.field_f58e6118,
          std::uint32_t(reinterpret_cast<std::uintptr_t>(&command)));
    const auto previous_air = read<std::uint8_t>(state, 0x109) != 0;
    const auto previous_time = read<float>(state, 0x110);
    layers = read<std::byte*>(descriptor, 0);
    if (count > 3)
        prepare_idle_layer(owner, player, state, command, layers, b);
    animation::update_landing_layer(player, state, b.helpers);
    layers = read<std::byte*>(descriptor, 0);
    prepare_landing_tag(owner, player, state, layers, b);
    std::array<std::uint32_t, 6> saved;
    if (count > 2) {
        saved[0] = read<std::uint32_t>(layers, 0x90);
        saved[1] = read<std::uint32_t>(layers, 0x9C);
        saved[2] = read<std::uint32_t>(layers, 0x58);
        saved[3] = read<std::uint32_t>(layers, 0x64);
    }
    if (count > 3) {
        saved[4] = read<std::uint32_t>(layers, 0xC8);
        saved[5] = read<std::uint32_t>(layers, 0xD4);
    }
    const auto saved_command = read<std::uint32_t>(player, b.offsets.field_f58e6118);
    if (replay) {
        const auto saved_state = read<void*>(player, b.offsets.animation.animation_state);
        *b.animation_update_flag = 1;
        write(player, b.offsets.animation.animation_state, state);
        auto timing = b.animation_timing;
        timing.frame = *b.frame_number;
        timing.current_time = *b.current_time;
        timing.interval = *b.interval;
        animation::update_animation_state(state, *replay_angles, timing, b.animation_engine);
        write(player, b.offsets.animation.animation_state, saved_state);
        *b.animation_update_flag = 0;
        write(player, b.offsets.field_f58e6118, saved_command);
    } else {
        const auto previous_body = read<std::uint32_t>(player, b.offsets.animation.field_a2a8247f);
        write(player, b.offsets.animation.field_a2a8247f, *b.body_yaw);
        write(player, b.offsets.field_f58e6118,
              std::uint32_t(reinterpret_cast<std::uintptr_t>(&command)));
        if (auto* current_state = read<void*>(player, b.offsets.animation.animation_state))
            if (read<float>(current_state, 0x6C) == *b.current_time)
                write(current_state, 0x6C, *b.current_time - *b.interval);
        *b.animation_update_flag = 1;
        auto** table = b.original_player_table;
        if (b.resolve_original_player_table)
            table = static_cast<engine::VtableHookMetadata*>((*static_cast<void***>(player))[-2])
                        ->original_table;
        reinterpret_cast<engine::MemberFunction<void>>(table[224])(player);
        *b.animation_update_flag = 0;
        b.animation_timing.clear_reset_byte();
        write(player, b.offsets.field_f58e6118, saved_command);
        write(player, b.offsets.animation.field_a2a8247f, previous_body);
    }
    if (count > 3) {
        write(layers, 0xD4, saved[5]);
        write(layers, 0xC8, saved[4]);
    }
    if (count > 2) {
        write(layers, 0x64, saved[3]);
        write(layers, 0x58, saved[2]);
        write(layers, 0x9C, saved[1]);
        write(layers, 0x90, saved[0]);
    }
    update_post_layers(owner, player, state, read<std::byte*>(descriptor, 0), count, replay, b);
    const auto vertical = read<float>(player, b.offsets.field_36375c23 + 8);
    animation::update_lower_body_yaw_timer(command.angles[1], read<float>(state, 0xEC), vertical,
                                           *b.current_time, read<float>(state, 0x80),
                                           *b.body_yaw_time, *b.body_yaw);
    animation::update_post_animation_layer(player, state, previous_air, previous_time, b.helpers);
}
} // namespace skeet::client::restricted_detail
