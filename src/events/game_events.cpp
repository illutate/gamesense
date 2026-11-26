#include "skeet/events/game_events.hpp"
#include <bit>

namespace skeet::events {
namespace {
constexpr std::uint32_t userid = 0x8361150E, attacker = 0x455B4DBF;
constexpr std::uint32_t weapon = 0xFDF97DB0, damage = 0x32130FCF, hitgroup = 0xB8B40BAC;
constexpr std::uint32_t coordinates[] = {0x6FC60D76, 0x6EC61305, 0x6DC60E5C};
bool active(const GameEventState& state) {
    return state.enabled && state.has_pending_shots;
}
bool contains_fire(const GameEventState& state) {
    for (const auto& event : state.feedback)
        if (event.kind == ShotEventKind::fire)
            return true;
    return false;
}
}

std::uint32_t event_hash(const char* text) noexcept {
    std::uint32_t result = 0x811C9DC5;
    if (text)
        for (auto cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
            result = (result ^ *cursor) * 0x1000193;
    return result ^ 0x92CA5DF1;
}

bool FieldQuery::integer(const char* name, std::uint32_t input) noexcept {
    if (type != FieldType::integer || event_hash(name) != name_hash)
        return true;
    value.integer = input;
    return false;
}
bool FieldQuery::floating(const char* name, float input) noexcept {
    if (type != FieldType::floating || event_hash(name) != name_hash)
        return true;
    value.floating = input;
    return false;
}
bool FieldQuery::string(const char* name, const char* input) noexcept {
    if (type != FieldType::string || event_hash(name) != name_hash)
        return true;
    value.string = input;
    return false;
}
bool FieldQuery::boolean(const char* name, bool input) noexcept {
    if (type != FieldType::boolean || event_hash(name) != name_hash)
        return true;
    value.boolean = input;
    return false;
}

std::uint32_t ShotEvent::payload_words() const noexcept {
    switch (kind) {
    case ShotEventKind::fire:
    case ShotEventKind::death:
        return 1;
    case ShotEventKind::impact:
        return 3;
    case ShotEventKind::hurt:
        return 4;
    }
    return 0;
}

void weapon_fire(GameEventState& state, GameEventReader& event, EventPlayerIndices& players) {
    if (!active(state))
        return;
    FieldValue user, item;
    if (event.read(userid, FieldType::integer, user))
        return;
    if (event.read(weapon, FieldType::string, item))
        return;
    const auto index = players.player_for_user_id(user.integer);
    if (index != players.local_player_index())
        return;
    ShotEvent output;
    output.kind = ShotEventKind::fire;
    output.payload[0] = event_hash(item.string);
    state.feedback.push_back(output);
}

void bullet_impact(GameEventState& state, GameEventReader& event, EventPlayerIndices& players) {
    if (!active(state) || !contains_fire(state))
        return;
    FieldValue user;
    std::array<FieldValue, 3> position;
    if (event.read(userid, FieldType::integer, user))
        return;
    for (unsigned axis = 0; axis != 3; ++axis)
        if (event.read(coordinates[axis], FieldType::floating, position[axis]))
            return;
    const auto index = players.player_for_user_id(user.integer);
    if (index != players.local_player_index())
        return;
    ShotEvent output;
    output.kind = ShotEventKind::impact;
    for (unsigned axis = 0; axis != 3; ++axis)
        output.payload[axis] = std::bit_cast<std::uint32_t>(position[axis].floating);
    state.feedback.push_back(output);
}

void player_hurt(GameEventState& state, GameEventReader& event, EventPlayerIndices& players) {
    if (!active(state) || !contains_fire(state))
        return;
    FieldValue user, source, item, amount, group;
    if (event.read(userid, FieldType::integer, user))
        return;
    if (event.read(attacker, FieldType::integer, source))
        return;
    if (event.read(weapon, FieldType::string, item))
        return;
    if (event.read(damage, FieldType::integer, amount))
        return;
    if (event.read(hitgroup, FieldType::integer, group))
        return;
    const auto victim = players.player_for_user_id(user.integer);
    if (static_cast<std::uint32_t>(victim) - 1 > 63)
        return;
    const auto local = players.local_player_index();
    if (victim == local)
        return;
    if (players.player_for_user_id(source.integer) != local)
        return;
    ShotEvent output;
    output.kind = ShotEventKind::hurt;
    output.payload[0] = static_cast<std::uint32_t>(victim);
    output.payload[1] = group.integer;
    output.payload[2] = amount.integer;
    output.payload[3] = event_hash(item.string);
    state.feedback.push_back(output);
}

void player_death(GameEventState& state, GameEventReader& event, EventPlayerIndices& players) {
    if (!active(state))
        return;
    FieldValue user;
    if (event.read(userid, FieldType::integer, user))
        return;
    ShotEvent output;
    output.kind = ShotEventKind::death;
    output.payload[0] = static_cast<std::uint32_t>(players.player_for_user_id(user.integer));
    state.feedback.push_back(output);
}
} // namespace skeet::events
