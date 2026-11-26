#include "skeet/lagcomp/prediction_scope.hpp"
#include <cstring>

namespace skeet::lagcomp {
namespace {
void* terminal(void* object) noexcept {
    for (;;) {
        void* parent;
        std::memcpy(&parent, static_cast<std::byte*>(object) + 0x1C, sizeof(parent));
        if (object == parent)
            return object;
        object = parent;
    }
}
std::uint32_t pointer_word(const void* cell) noexcept {
    std::uint32_t value;
    std::memcpy(&value, cell, sizeof value);
    return value;
}
void pointer_word(void* cell, std::uint32_t value) noexcept {
    std::memcpy(cell, &value, sizeof value);
}
}
std::uint32_t read_prediction_convar(void* object) noexcept {
    object = terminal(object);
    std::uint32_t result;
    std::memcpy(&result, static_cast<std::byte*>(object) + 0x2C, 4);
    return result ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
void write_prediction_convar(void* object, std::uint32_t bits) noexcept {
    object = terminal(object);
    bits ^= std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
    std::memcpy(static_cast<std::byte*>(object) + 0x2C, &bits, 4);
    std::memcpy(static_cast<std::byte*>(object) + 0x3C, &bits, 4);
}
namespace {
std::uint32_t save_and_write_prediction_convar(void* object, std::uint32_t new_bits) noexcept {
    object = terminal(object);
    const auto cookie = std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
    std::uint32_t old_bits;
    std::memcpy(&old_bits, static_cast<std::byte*>(object) + 0x2C, 4);
    new_bits ^= cookie;
    std::memcpy(static_cast<std::byte*>(object) + 0x2C, &new_bits, 4);
    std::memcpy(static_cast<std::byte*>(object) + 0x3C, &new_bits, 4);
    return old_bits ^ cookie;
}
}
PredictionScope::PredictionScope(const PredictionTargets& input, std::uint32_t player,
                                 std::uint32_t command, std::uint32_t seed) noexcept
    : targets(input) {
    bytes[0] = *targets.in_prediction;
    *targets.in_prediction = 1;
    bytes[1] = *targets.first_time_predicted;
    *targets.first_time_predicted = 0;
    bytes[2] = *targets.command_predicted;
    *targets.command_predicted = 1;
    words[0] = *targets.random_seed;
    *targets.random_seed = seed;
    // These are actual engine pointer cells. Copy their32-bit representation
    // without accessing a C++ pointer object through an integer lvalue.
    words[1] = pointer_word(targets.prediction_player);
    pointer_word(targets.prediction_player, player);
    words[2] = pointer_word(targets.player_current_command);
    pointer_word(targets.player_current_command, command);
    words[3] = save_and_write_prediction_convar(targets.convar_aaa77b8a, 0);
    words[4] = save_and_write_prediction_convar(targets.convar_de58ce34, 0x7F7FFFFF);
    words[5] = *targets.field_a7f8_08;
    vector = *targets.player_field_6610746e;
}
void PredictionScope::restore() noexcept {
    if (targets.player_field_6610746e) {
        *targets.player_field_6610746e = vector;
        targets.player_field_6610746e = nullptr;
    }
    if (targets.field_a7f8_08) {
        *targets.field_a7f8_08 = words[5];
        targets.field_a7f8_08 = nullptr;
    }
    if (targets.convar_de58ce34) {
        write_prediction_convar(targets.convar_de58ce34, words[4]);
        targets.convar_de58ce34 = nullptr;
    }
    if (targets.convar_aaa77b8a) {
        write_prediction_convar(targets.convar_aaa77b8a, words[3]);
        targets.convar_aaa77b8a = nullptr;
    }
    if (targets.player_current_command) {
        pointer_word(targets.player_current_command, words[2]);
        targets.player_current_command = nullptr;
    }
    if (targets.prediction_player) {
        pointer_word(targets.prediction_player, words[1]);
        targets.prediction_player = nullptr;
    }
    if (targets.random_seed) {
        *targets.random_seed = words[0];
        targets.random_seed = nullptr;
    }
    if (targets.command_predicted) {
        *targets.command_predicted = bytes[2];
        targets.command_predicted = nullptr;
    }
    if (targets.first_time_predicted) {
        *targets.first_time_predicted = bytes[1];
        targets.first_time_predicted = nullptr;
    }
    if (targets.in_prediction) {
        *targets.in_prediction = bytes[0];
        targets.in_prediction = nullptr;
    }
}
} // namespace skeet::lagcomp
