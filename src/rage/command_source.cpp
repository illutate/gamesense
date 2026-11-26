#include "skeet/rage/command_source.hpp"
#include "skeet/rage/post_firing.hpp"
#include <stdexcept>

namespace skeet::rage {
PreambleState& CommandEndpoint::state() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->state;
    return std::get<CommandSource*>(source_)->state();
}
AcquisitionStorage& CommandEndpoint::acquisition() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->acquisition;
    return std::get<CommandSource*>(source_)->acquisition();
}
std::int32_t& CommandEndpoint::shot_index() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->post_firing.shot_index;
    return std::get<CommandSource*>(source_)->shot_index();
}
esp::HitboxHighlightState& CommandEndpoint::action_state() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->tail.action_state;
    return std::get<CommandSource*>(source_)->action_state();
}
std::array<animation::AnimationAngleSample, 2>& CommandEndpoint::pending_angles() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->phase6.pending_angles;
    return std::get<CommandSource*>(source_)->pending_angles();
}
PredictionMaskOwner* const& CommandEndpoint::prediction_masks() const noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        return (*value)->post_firing.movement.prediction.owner;
    return std::get<CommandSource*>(source_)->prediction_masks();
}
void CommandEndpoint::require_factory(const AimbotSettings& settings, const GlobalSettings& global,
                                      const esp::AimbotVisualizationConfig& visualization,
                                      const esp::HitboxHighlightState& highlight) const {
    if (auto* value = std::get_if<CommandOwner*>(&source_)) {
        const auto& c = **value;
        if (&c.preamble.weapon_settings != &settings || &c.preamble.settings != &global ||
            &c.tail.action_state != &highlight || &c.tail.action.visualization != &visualization ||
            &c.single_target.preparation.state != &c.state ||
            &c.multiple_targets.preparation.state != &c.state)
            throw std::invalid_argument(
                "RAGE constructor requires canonical command settings and state");
    } else
        std::get<CommandSource*>(source_)->require_factory(settings, global, visualization,
                                                           highlight);
}
void CommandEndpoint::phase2(void* player, void* weapon, client::Command& command,
                             std::array<float, 3>& angles, std::uint8_t& send) {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        process_rage_command_phase2(**value, player, weapon, command, angles, send);
    else
        std::get<CommandSource*>(source_)->phase2(player, weapon, command, angles, send);
}
void CommandEndpoint::process(client::CommandFrame& frame, void* player, client::Command& command,
                              std::uint8_t& send) {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        process_rage_command(**value, frame, player, command, send);
    else
        std::get<CommandSource*>(source_)->process(frame, player, command, send);
}
void CommandEndpoint::phase6(const client::CommandFrame& frame, const client::Command& command) {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        process_rage_phase6(**value, frame, command);
    else
        std::get<CommandSource*>(source_)->phase6(frame, command);
}
void CommandEndpoint::capture(const client::CommandFrame& frame) noexcept {
    if (auto* value = std::get_if<CommandOwner*>(&source_))
        capture_rage_command_frame(**value, frame);
    else
        std::get<CommandSource*>(source_)->capture(frame);
}
} //namespace skeet::rage
