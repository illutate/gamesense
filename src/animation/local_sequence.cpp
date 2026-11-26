#include "skeet/animation/local_helpers.hpp"
#include "skeet/client/prediction_history_owner.hpp"
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
std::uint16_t symbol(const char* text, const LocalAnimationBindings& bindings) {
    std::uint16_t value = 0xFFFFU;
    if (bindings.find_symbol)
        bindings.find_symbol(bindings.symbol_table, &value, text);
    if (value == 0xFFFFU) {
        value = 0xFFFFU;
        if (bindings.add_symbol)
            bindings.add_symbol(bindings.symbol_table, &value, text);
    }
    return value;
}
std::array<std::uint32_t, 8> activity_context(void* player,
                                              const LocalAnimationBindings& bindings) {
    std::array<std::uint32_t, 8> words;
    words[0] = std::uint32_t(reinterpret_cast<std::uintptr_t>(bindings.activity_interface));
    std::memcpy(words.data() + 1, bindings.activity_scratch.data(),
                sizeof(bindings.activity_scratch));
    words[7] = std::uint32_t(reinterpret_cast<std::uintptr_t>(player));
    return words;
}
}
const char* weapon_sequence_prefix(void* weapon, const LocalAnimationBindings& bindings) {
    std::int32_t index = 0;
    if (weapon) {
        const auto kind = engine::invoke<std::int32_t>(weapon, 455);
        const auto identifier = field<std::uint16_t>(weapon, bindings.offsets.weapon_identifier);
        if (identifier == 37)
            return client::prediction_history_sequence_string(*bindings.sequence_owner, 4);
        if (identifier == 27)
            index = 3;
        else if (identifier == 31)
            index = 1;
        else if (kind == 11 || identifier == 70 || identifier == 85)
            index = 9;
        else if (identifier == 72 || identifier == 20)
            index = 7;
        else if (kind != 16 && kind >= 0)
            index = kind;
    }
    if (index > 10)
        index = 10;
    return client::prediction_history_sequence_string(*bindings.sequence_owner,
                                                      11 + std::size_t(index));
}
std::int32_t select_symbol_weighted_sequence(void* player, std::int32_t layer,
                                             std::int32_t activity,
                                             const LocalAnimationBindings& bindings) {
    const auto* descriptor = static_cast<const std::byte*>(player) + bindings.offsets.layers;
    const auto begin = field<std::uintptr_t>(descriptor, 0);
    if (!(begin + std::uintptr_t(std::uint32_t(layer) * 0x38U)))
        return -1;
    auto* studio = field<void*>(player, bindings.offsets.studio_header);
    if (!studio || !field<void*>(studio, 0) || !bindings.initialize_mapping ||
        !bindings.select_symbol_sequence || !bindings.add_symbol || !bindings.find_symbol ||
        !bindings.symbol_table)
        return -1;
    auto* mapping = field<void*>(studio, 0x58);
    if (!mapping) {
        mapping = bindings.initialize_mapping(studio);
        store(studio, 0x58, mapping);
        if (!mapping)
            return -1;
    }
    auto* animation = field<void*>(player, bindings.offsets.animation_state);
    std::array<std::uint16_t, 4> symbols;
    std::int32_t count = 0;
    const auto append = [&](const char* text) {
        const auto value = symbol(text, bindings);
        if (value != 0xFFFFU)
            symbols[std::size_t(count++)] = value;
    };
    const auto handle = field<std::uint32_t>(player, bindings.offsets.active_weapon);
    if (handle != 0xFFFFFFFFU) {
        if (auto* weapon = engine::invoke<void*>(bindings.entity_list, 4, handle))
            append(weapon_sequence_prefix(weapon, bindings));
    }
    const auto modifiers = field<std::uint8_t>(animation, 0x32B);
    if (modifiers & 1U)
        append("moving");
    if (modifiers & 2U)
        append("crouch");
    if (!count)
        return -1;
    return bindings.select_symbol_sequence(mapping, studio, activity, symbols.data(), count);
}
std::int32_t map_animation_activity(void* player, const char* name,
                                    const LocalAnimationBindings& bindings) {
    if (!bindings.activity_interface)
        return -1;
    const auto context = activity_context(player, bindings);
    using Map = std::int32_t (*)(const std::uint32_t*, const char*);
    const auto function = field<Map>(bindings.activity_interface, 0x64);
    return function(context.data(), name);
}
std::int32_t select_prediction_sequence(void* player, std::int32_t activity, std::int32_t seed,
                                        const LocalAnimationBindings& bindings) {
    if (!bindings.activity_interface)
        return -1;
    std::int32_t previous_seed = 0;
    if (bindings.prediction_seed) {
        previous_seed = *bindings.prediction_seed;
        *bindings.prediction_seed = seed;
    }
    void* previous_player = nullptr;
    const bool published = bindings.prediction_player && bindings.local_player;
    if (published) {
        previous_player = *bindings.prediction_player;
        *bindings.prediction_player = bindings.local_player;
    }
    auto context = activity_context(player, bindings);
    const auto function = field<engine::MemberFunction<std::int32_t, std::int32_t>>(
        bindings.activity_interface, 0x5C);
    const auto result = function(context.data(), activity);
    if (published)
        *bindings.prediction_player = previous_player;
    if (bindings.prediction_seed)
        *bindings.prediction_seed = previous_seed;
    return result;
}
} // namespace skeet::animation
