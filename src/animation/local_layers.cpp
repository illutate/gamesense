#include "skeet/animation/local_helpers.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include <array>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void store(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
void* layer_five(void* player, const LocalAnimationBindings& bindings) noexcept {
    const auto begin = field<std::uintptr_t>(player, bindings.offsets.layers);
    return reinterpret_cast<void*>(begin + 0x118U);
}
void reset_layer(void* player, void* layer, std::int32_t sequence) {
    store(layer, 0x18, sequence);
    store(layer, 0x2C, std::uint32_t(0));
    store(layer, 0x20, std::uint32_t(0));
    const auto rate = engine::invoke<float>(player, 223, layer, sequence);
    store(layer, 0x28, rate);
}
}
void mark_animation_modifiers(void* state) noexcept {
    const bool moving = field<float>(state, 0xF8) > 0.25f;
    std::uint8_t flags = std::uint8_t(moving);
    if (field<float>(state, 0xA4) > 0.550000011920928955078125f)
        flags = std::uint8_t(moving) | 2U;
    store(state, 0x32B, flags);
}
float read_local_layer_weight(void* state, std::int32_t layer,
                              const LocalAnimationBindings& bindings) {
    return bindings.layer_weight.read(bindings.layer_weight.context, state, layer);
}
void update_landing_layer(void* player, void* state, const LocalAnimationBindings& bindings) {
    const auto* descriptor = static_cast<const std::byte*>(player) + bindings.offsets.layers;
    if (field<std::int32_t>(descriptor, 0xC) <= 5)
        return;
    const auto previous = field<std::uint8_t>(state, 0x108);
    const bool ground = (field<std::uint8_t>(player, bindings.offsets.player_flags) & 1U) ||
                        field<std::uint8_t>(player, bindings.offsets.additional_ground);
    const bool transitioned =
        !field<std::uint8_t>(state, 4) && previous != std::uint8_t(ground) && ground;
    if (!ground || field<std::uint8_t>(state, 0x109) || !transitioned)
        return;
    const bool long_air = field<float>(state, 0x110) > 1.0f;
    auto sequence = select_symbol_weighted_sequence(player, 5, long_air ? 0x3DD : 0x3DC, bindings);
    void* layer;
    if (sequence > 1) {
        layer = layer_five(player, bindings);
        if (!layer)
            return;
    } else {
        std::array<char, 64> name;
        ::strcpy_s(name.data(), name.size(),
                   client::prediction_history_sequence_string(*bindings.sequence_owner, 5));
        ::strcat_s(
            name.data(), name.size(),
            client::prediction_history_sequence_string(*bindings.sequence_owner, long_air ? 6 : 7));
        const auto modifiers = field<std::uint8_t>(state, 0x32B);
        if (modifiers & 2U)
            ::strcat_s(name.data(), name.size(),
                       client::prediction_history_sequence_string(*bindings.sequence_owner, 1));
        if (!long_air && (modifiers & 1U))
            ::strcat_s(name.data(), name.size(),
                       client::prediction_history_sequence_string(*bindings.sequence_owner, 0));
        sequence = map_animation_activity(player, name.data(), bindings);
        if (sequence <= 1)
            return;
        layer = layer_five(player, bindings);
    }
    reset_layer(player, layer, sequence);
}
void update_post_animation_layer(void* player, void* state, bool previous_in_air,
                                 float unused_previous_time,
                                 const LocalAnimationBindings& bindings) {
    (void)unused_previous_time;
    const auto* descriptor = static_cast<const std::byte*>(player) + bindings.offsets.layers;
    if (field<std::int32_t>(descriptor, 0xC) <= 5)
        return;
    if (field<std::uint8_t>(state, 0x108)) {
        if (previous_in_air) {
            const auto* layer = layer_five(player, bindings);
            bool clear = field<float>(layer, 0x20) == 0.0f;
            if (!clear) {
                const auto sequence = field<std::int32_t>(layer, 0x18);
                clear = (bindings.sequence_activity ? bindings.sequence_activity(player, sequence)
                                                    : -1) != 0x3DB;
            }
            if (clear)
                store(state, 0x10B, std::uint8_t(0));
        }
        if (!field<std::uint8_t>(state, 0x109) && !field<std::uint8_t>(state, 0x10B) &&
            field<float>(state, 0x12C) <= 0.0f)
            store(field<void*>(descriptor, 0), 0x138, std::uint32_t(0));
        return;
    }
    if (field<std::uint8_t>(state, 0x128) || !field<std::uint8_t>(state, 0x121) ||
        field<std::uint8_t>(state, 0x10B))
        return;
    const auto sequence = select_symbol_weighted_sequence(player, 5, 0x3DA, bindings);
    if (sequence <= 1)
        return;
    auto* layer = layer_five(player, bindings);
    if (layer)
        reset_layer(player, layer, sequence);
}
} // namespace skeet::animation
