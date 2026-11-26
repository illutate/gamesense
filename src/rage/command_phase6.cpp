#include "skeet/rage/command_phase6.hpp"
#include "skeet/lagcomp/update_timing.hpp"
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool rules_block_command(const CommandPhase6Bindings& bindings) noexcept { //433DF22C
    return bindings.game_rules && *bindings.game_rules &&
           field<std::uint8_t>(*bindings.game_rules, bindings.rule_guard) != 0;
}
}

void publish_command_animation_angles(std::array<animation::AnimationAngleSample, 2>& samples,
                                      std::int32_t number,
                                      const std::array<float, 3>& angles) noexcept {
    if (number != samples[0].frame)
        std::memcpy(&samples[1], &samples[0], sizeof samples[0]);
    samples[0].frame = number;
    std::memcpy(samples[0].angles.data(), angles.data(), sizeof samples[0].angles);
}

void process_rage_phase6(const client::CommandFrame& frame, const client::Command& command,
                         const CommandPhase6Bindings& bindings) noexcept {
    if (!frame.weapon_address || !frame.weapon_data_address)
        return;
    if (rules_block_command(bindings))
        return;
    const auto* weapon = reinterpret_cast<const void*>(std::uintptr_t(frame.weapon_address));
    if (!lagcomp::history_weapon_is_firearm(
            field<std::uint16_t>(weapon, bindings.weapon_identifier)))
        return;
    if (!(frame.flags & 0x100000U) &&
        !((frame.flags & 0x400U) && (command.movement.buttons & 1U)) &&
        !((frame.flags & 2U) && (command.movement.buttons & 0x800U) &&
          (frame.weapon_id_d8 == 64 || frame.weapon_kind_124 == 0)))
        return;
    publish_command_animation_angles(bindings.pending_angles, frame.command_number_ac,
                                     command.angles);
}
} // namespace skeet::rage
