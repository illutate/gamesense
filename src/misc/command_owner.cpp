#include "skeet/misc/command_owner.hpp"
#include <type_traits>

namespace skeet::misc {
void KnifeCommandBindings::process(const KnifeConfig& settings, client::CommandFrame& frame,
                                   client::Command& command, void* player, std::uint8_t& packet,
                                   std::uint32_t& flags) const {
    std::visit(
        [&](auto* source) {
            using Source = std::remove_cv_t<std::remove_pointer_t<decltype(source)>>;
            if constexpr (std::is_same_v<Source, KnifeRuntimeBindings>)
                process_knife_command(settings, frame, command, player, packet, flags, *source);
            else
                source->process(settings, frame, command, player, packet, flags);
        },
        source_);
}
void dispatch_command_owner(CommandOwner& value, std::uint32_t phase, client::CommandFrame* frame,
                            void* player, void* weapon, client::Command& command,
                            std::uint8_t& send_packet, std::array<float, 3>& incoming_angles,
                            InputBitmap keys) {
    std::visit(
        [&](auto& owner) {
            using Owner = std::remove_cvref_t<decltype(owner)>;
            if constexpr (std::is_same_v<Owner, QuickSwitchRuntime*>) {
                if (phase == 2)
                    owner->process_phase2(player, weapon, command, send_packet);
            } else if constexpr (std::is_same_v<Owner, RebuyFixRuntime*>) {
                if (phase == 2) {
                    owner->local_player = player;
                    owner->update(command.movement);
                }
            } else if constexpr (std::is_same_v<Owner, AutomaticWeaponsCommandOwner>) {
                if (phase == 4)
                    process_automatic_weapons(owner.state, owner.settings, owner.bindings, *frame,
                                              player, command, keys);
            } else if constexpr (std::is_same_v<Owner, KnifeCommandOwner>) {
                if (phase == 4)
                    owner.bindings.process(owner.settings, *frame, command, player, send_packet,
                                           owner.scheduler_flags);
            } else if constexpr (std::is_same_v<Owner, ZeusCommandOwner>) {
                if (phase == 4)
                    process_zeus(owner.state, owner.settings, owner.bindings, *frame, player,
                                 command);
            } else if constexpr (std::is_same_v<Owner, BlockbotCommandOwner>) {
                if (phase == 2) {
                    MovementAngles angles{incoming_angles[0], incoming_angles[1],
                                          incoming_angles[2]};
                    process_blockbot(owner.state, owner.enabled.value != 0,
                                     owner.hotkey.value.binding, keys, player, command, angles,
                                     owner.bindings);
                    incoming_angles = {angles.pitch, angles.yaw, angles.roll};
                }
            }
        },
        value);
}
std::int32_t command_owner_budget(const CommandOwner&, std::int32_t incoming) noexcept {
    return incoming;
}
} //namespace skeet::misc
