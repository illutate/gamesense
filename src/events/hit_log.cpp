#include "skeet/events/hit_log.hpp"
#include <bit>
#include <cstdio>
#include <cstring>

namespace skeet::events {
std::array<char, 128> player_log_name(std::int32_t index, HitLogPlayerInfo& engine) {
    std::array<char, 128> result{};
    std::array<std::byte, 0x158> info{};
    if (engine.player_info(index, info)) {
        std::memcpy(result.data(), info.data() + 0x10, result.size());
        for (auto& character : result) {
            if (!character)
                break;
            if (character == '\a' || character == '\t' || character == '\n' || character == '\r')
                character = ' ';
        }
        result[20] = 0;
    } else {
        ::sprintf_s(result.data(), result.size(), "%d", index);
    }
    return result;
}
void HitLog::player_hurt(GameEventReader& event, HitLogPlayerInfo& players, misc::NormalLog& log) {
    if (!settings.damage.value)
        return;
    FieldValue attacker, victim, group, damage, health;
    if (event.read(0x455B4DBF, FieldType::integer, attacker))
        return;
    if (event.read(0x8361150E, FieldType::integer, victim))
        return;
    if (event.read(0xB8B40BAC, FieldType::integer, group))
        return;
    if (event.read(0x32130FCF, FieldType::integer, damage))
        return;
    if (event.read(0xF952B07E, FieldType::integer, health))
        return;
    const auto source = players.player_for_user_id(attacker.integer);
    if (source != players.local_player_index())
        return;
    const auto target = players.player_for_user_id(victim.integer);
    if (static_cast<std::uint32_t>(target) - 1 > 63)
        return;
    if (target == players.local_player_index())
        return;
    if (group.integer > 8)
        return;
    const auto name = player_log_name(target, players);
    const auto* const hitgroup = hitgroups[group.integer].begin;
    std::array<char, 54> decoded_format; // native ESP+62..+97
    misc::decode_native_text(format, decoded_format.data());
    log.format(decoded_format.data(), name.data(), hitgroup,
               std::bit_cast<std::int32_t>(damage.integer),
               std::bit_cast<std::int32_t>(health.integer));
}
} // namespace skeet::events
