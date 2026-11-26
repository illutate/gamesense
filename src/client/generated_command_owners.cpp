#include "skeet/client/generated_command_owners.hpp"
#include "skeet/movement/command_runtime.hpp"
#include "skeet/anti_aim/command_owner.hpp"
#include <cstring>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

void generated_micro_movement(anti_aim::CommandOwner& owner, void* player,
                              Command& command) noexcept {
    const auto& offsets = owner.offsets;
    anti_aim::PrecalculationPlayer snapshot{};
    snapshot.duck_3309c0cf = field<float>(player, offsets.duck_3309c0cf);
    float interval = 0.0f;
    if (command.movement.buttons & 4u) {
        interval = *owner.globals.interval;
        snapshot.rate_fb7e3404 = field<float>(player, offsets.rate_fb7e3404);
    } else {
        if (snapshot.duck_3309c0cf != 1.0f ||
            snapshot.duck_3309c0cf != owner.precalculation.previous_duck_1a4) {
            snapshot.rate_fb7e3404 = field<float>(player, offsets.rate_fb7e3404);
            interval = *owner.globals.interval;
        }
        snapshot.field_6b1a59b5 = field<std::uint8_t>(player, offsets.ducking_6b1a59b5) != 0;
        if (!snapshot.field_6b1a59b5)
            snapshot.flags_e2fbe695 = field<std::uint32_t>(player, offsets.player_flags_e2fbe695);
    }
    anti_aim::apply_micro_movement(owner.precalculation, snapshot, interval, command);
}
}

void process_generated_movement_command(const GeneratedCommandOwnersBindings& bindings,
                                        CommandFrame& frame, void* player, Command& command,
                                        std::uint8_t& send_packet) {
    auto* const owner = bindings.movement_owner;
    if (!owner)
        return;
    owner->duck_peek.choked = frame.choked_15c;
    process_movement_phase_one(*owner, command, bindings.infinite_duck != 0);
    process_movement_command(*owner, player,
                             reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address)), command,
                             frame.angles_118, send_packet, owner->feedback, bindings.movement);
    owner->feedback.was_grounded = static_cast<std::uint8_t>((frame.flags >> 18) & 1u);
    update_quick_peek_shot_counter(owner->quick_peek,
                                   field<std::int32_t>(player, bindings.shot_counter_8cfc633a));
}

void process_generated_angle_command(const GeneratedCommandOwnersBindings& bindings,
                                     CommandFrame& frame, void* player, Command& command,
                                     std::uint8_t& send_packet) {
    auto* const owner = bindings.angle_owner;
    if (!owner)
        return;
    if (!(owner->globals.scheduler_flags & 0x800u)) {
        const auto& velocity = frame.velocity_150;
        const auto speed_squared =
            (velocity[0] * velocity[0] + velocity[1] * velocity[1]) + velocity[2] * velocity[2];
        if (4.0f > speed_squared)
            generated_micro_movement(*owner, player, command);
    }
    anti_aim::command_owner_phase4(*owner, frame, player, command, send_packet);
}
} //namespace skeet::client
