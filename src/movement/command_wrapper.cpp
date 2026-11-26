#include "skeet/movement/command_wrapper.hpp"
#include "skeet/movement/command_bindings.hpp"
#include <cstring>

namespace skeet {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}

void movement_owner_phase1(MovementCommandPhaseOwner& owner, client::Command& command) {
    process_movement_phase_one(owner.state, command, owner.bindings.command.infinite_duck != 0);
}

void movement_owner_phase2(MovementCommandPhaseOwner& owner, void* player, void* weapon,
                           client::Command& command, std::array<float, 3>& angles,
                           std::uint8_t& send_packet) {
    auto& state = owner.state;
    const auto& bindings = owner.bindings;
    const auto& live = bindings.command;
    const auto* const count = bindings.choked_commands;
    state.callback_flags_1dc &= ~1u;
    if (*count == 0) {
        // The guard reads this field even though both valid-player branches
        // join at the same origin copy. Its other branch is invalid code.
        [[maybe_unused]] volatile auto movement_flags =
            field<std::uint32_t>(player, bindings.movement_flags_38ca5228);
        state.previous_packet_origin = field<MovementVector>(player, live.fields.origin_a9ee6062);
    }
    state.duck_peek.choked = *count;
    process_movement_command(state, player, weapon, command, angles, send_packet, state.feedback,
                             live);
    state.feedback.was_grounded =
        static_cast<std::uint8_t>(movement_command_player_flags(player, live) & 1u);
    update_quick_peek_shot_counter(state.quick_peek,
                                   field<std::int32_t>(player, live.fields.shot_counter_8cfc633a));
}

void movement_owner_phase4(MovementCommandPhaseOwner& owner, client::CommandFrame& frame,
                           client::Command& command) {
    MovementFrame projected{frame.flags, frame.player_flags_f8};
    const MovementSettings settings{owner.state.config.jump_at_edge.value != 0,
                                    owner.state.callback_flags_1dc};
    post_prediction(settings, projected, command.movement, owner.bindings.jump_at_edge_key,
                    owner.bindings.command.input);
    frame.flags = projected.packed_flags;
}

} //namespace skeet
