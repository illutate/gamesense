#include "skeet/rage/aim_fire_event.hpp"
#include "skeet/rage/acquisition.hpp"
#include "skeet/script/bridge.hpp"
#include "skeet/events/event_name_storage.hpp"
#include <string_view>

namespace skeet::rage {
namespace {
struct AimFireEvent {
    const TargetCandidate& candidate;
    const AcquisitionEvaluation& evaluation;
    std::int32_t index;
};

void field_key(script::Bridge& bridge, std::string_view name) {
    bridge.api->pushlstring(bridge.state, name.data(), name.size());
}
void field_key(script::Bridge& bridge, events::EventNameRef name, std::size_t length) {
    bridge.api->pushlstring(bridge.state, name.get(), length);
}
void integer_field(script::Bridge& bridge, std::string_view name, std::int32_t value) {
    field_key(bridge, name);
    bridge.api->pushinteger(bridge.state, value);
    bridge.api->settable(bridge.state, -3);
}
void integer_field(script::Bridge& bridge, events::EventNameRef name, std::size_t length,
                   std::int32_t value) {
    field_key(bridge, name, length);
    bridge.api->pushinteger(bridge.state, value);
    bridge.api->settable(bridge.state, -3);
}
void number_field(script::Bridge& bridge, std::string_view name, float value) {
    field_key(bridge, name);
    bridge.api->pushnumber(bridge.state, static_cast<double>(value));
    bridge.api->settable(bridge.state, -3);
}
void number_field(script::Bridge& bridge, events::EventNameRef name, std::size_t length,
                  float value) {
    field_key(bridge, name, length);
    bridge.api->pushnumber(bridge.state, static_cast<double>(value));
    bridge.api->settable(bridge.state, -3);
}
void boolean_field(script::Bridge& bridge, std::string_view name, bool value) {
    field_key(bridge, name);
    bridge.api->pushboolean(bridge.state, value);
    bridge.api->settable(bridge.state, -3);
}
void boolean_field(script::Bridge& bridge, events::EventNameRef name, std::size_t length,
                   bool value) {
    field_key(bridge, name, length);
    bridge.api->pushboolean(bridge.state, value);
    bridge.api->settable(bridge.state, -3);
}

int marshal_aim_fire(script::Bridge& bridge, void* context) { //4332A6B4 /4336A85F
    const auto& event = *static_cast<const AimFireEvent*>(context);
    const auto& candidate = event.candidate;
    const auto& point = candidate.primary;
    bridge.api->createtable(bridge.state, 0, 18);
    integer_field(bridge, "id", event.index);
    integer_field(bridge, "target", static_cast<std::int32_t>(candidate.entity_index));
    number_field(bridge, "x", point.position[0]);
    number_field(bridge, "y", point.position[1]);
    number_field(bridge, "z", point.position[2]);
    integer_field(bridge, bridge.name(events::EventName::hitgroup), 8, point.hitgroup);
    integer_field(bridge, bridge.name(events::EventName::damage), 6, point.damage);
    // Native MULSS rounds to a float before the Lua double conversion.
    const float percent = point.chance * 100.0f;
    number_field(bridge, bridge.name(events::EventName::hit_chance), 10, percent);
    boolean_field(bridge, bridge.name(events::EventName::boosted), 7, (point.flags & 4U) != 0);
    boolean_field(bridge, bridge.name(events::EventName::high_priority), 13, false);
    integer_field(bridge, "tick", event.evaluation.requested_tick);
    boolean_field(bridge, "teleported", false);
    boolean_field(bridge, "interpolated", false);
    boolean_field(bridge, "extrapolated", event.evaluation.extrapolated);
    number_field(bridge, "backtrack", 0.0f);
    return 1;
}
}

int dispatch_aim_fire_event(script::Bridge& bridge, client::PlayerStateCache& players,
                            const client::CommandFrame&, const TargetCandidate& candidate,
                            const AcquisitionEvaluation& evaluation, std::int32_t shot_index) {
    if (!bridge.state)
        return 0;
    (void)players.get(candidate.entity_index); //4340C8ED: result is discarded
    AimFireEvent event{candidate, evaluation, shot_index};
    return script::dispatch_callbacks(bridge, bridge.name(events::EventName::aim_fire).get(),
                                      marshal_aim_fire, &event);
}
} // namespace skeet::rage
