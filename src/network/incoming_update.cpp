#include "skeet/network/incoming_update.hpp"
#include "skeet/network/active_sound_retention.hpp"
#include "skeet/misc/ping_spike_channel.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/esp/owner_runtime.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::network {
namespace {
IncomingUpdateRuntime* runtime;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
std::uint32_t convar_bits(const void* object, std::ptrdiff_t offset) {
    auto* owner = object;
    while (field<const void*>(owner, 0x1C) != owner)
        owner = field<const void*>(owner, 0x1C);
    return field<std::uint32_t>(owner, offset) ^
           std::uint32_t(reinterpret_cast<std::uintptr_t>(owner));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
bool recent_sound_history(const prediction::CommandHistoryState& state, float interval) {
    const auto scaled = _mm_div_ss(_mm_set_ss(field<float>(&state, 0x68)), _mm_set_ss(interval));
    const auto tick = _mm_cvtt_ss2si(_mm_add_ss(scaled, _mm_set_ss(0.5f)));
    const auto delta = subtract(subtract(field<std::int32_t>(&state, 0), tick), 1);
    const auto sign = std::uint32_t(delta) >> 31;
    const auto magnitude = (std::uint32_t(delta) ^ (0u - sign)) + sign;
    // Native ABS uses wrapped32-bit arithmetic then a signed JGE20.
    return std::bit_cast<std::int32_t>(magnitude) < 20;
}
void retain_active_sounds(IncomingUpdateGlobals& g, const IncomingUpdateBindings& b) {
    if (!g.sounds || convar_bits(b.convar_43469238.get(), 0x30) != 0 ||
        std::bit_cast<float>(convar_bits(b.convar_4346a850.get(), 0x2C)) == 0.0f ||
        !engine::invoke<bool>(g.engine_client, 26) || engine::invoke<bool>(g.engine_client, 82) ||
        engine::invoke<bool>(g.engine_client, 93))
        return;
    const auto index = engine::invoke<std::int32_t>(g.engine_client, 12);
    auto* player = engine::invoke<void*, std::int32_t>(g.entity_list, 3, index);
    if (!player || field<std::uint8_t>(player, b.rejected_9d920e35))
        return;
    const auto number = engine::invoke<std::int32_t>(g.engine_client, 205);
    const auto* state = prediction::CommandHistoryView::from_owner(g.prediction_owner).find(number);
    if (!state || state->weapon_identity == UINT32_MAX)
        return;
    const auto identity = std::int32_t(std::uint16_t(state->weapon_identity));
    std::int32_t skipped = 0;
    auto* tree = const_cast<esp::ActiveSoundTree*>(g.sounds);
    for (;;) {
        auto ordinal = std::int32_t(0);
        auto sound = first_active_sound(*g.sounds);
        for (; sound != -1; sound = next_active_sound(*g.sounds, sound)) {
            if (tree->nodes[sound].value.source_index == identity && ++ordinal > skipped)
                break;
        }
        if (sound == -1)
            return;
        if (recent_sound_history(*state, *g.interval)) {
            release_active_sound(*tree, sound);
            tree = const_cast<esp::ActiveSoundTree*>(g.sounds);
        } else
            ++skipped;
    }
}
void record_acknowledgement_timing(std::int32_t delta, IncomingUpdateGlobals& g,
                                   const IncomingUpdateBindings& b) {
    if (delta == 0)
        return;
    const auto number = *g.acknowledged;
    const auto& sent = b.sent_commands.records[number % 150];
    if (sent.command_number != number)
        return;
    const auto adjusted = sent.adjusted_tick;
    const auto time = engine::invoke<float>(g.engine_client, 14);
    const auto interval = field<float>(g.engine_globals, 0x20);
    const auto current =
        _mm_cvtt_ss2si(_mm_set_ss(float(static_cast<long double>(time) / interval + 0.5L)));
    auto elapsed = subtract(current, adjusted);
    if (elapsed < 0)
        elapsed = 0;
    else if (elapsed > 64)
        elapsed = 64;
    if (g.timing.count != 16)
        ++g.timing.count;
    g.timing.first = (g.timing.first - 1u) & 15u;
    g.timing.values[g.timing.first] = float(elapsed);
}
}
void process_incoming_update(void* object, IncomingUpdateGlobals& g,
                             const IncomingUpdateBindings& b) {
    auto* const player = g.local_player;
    if (auto* owner = b.sound_owner) {
        const auto& context = owner->sound_context();
        esp::process_sound_overlay(context.players.identity(player), owner->state().sounds,
                                   g.sounds, context);
    }
    retain_active_sounds(g, b);
    bool changed = false;
    reconcile_incoming_acknowledgement(player, changed, g, b);
    const std::int32_t* saved_sequence = nullptr;
    std::int32_t previous_sequence = 0;
    if (changed) {
        saved_sequence = g.sequence;
        previous_sequence = *saved_sequence;
        *const_cast<std::int32_t*>(saved_sequence) = -1;
    }
    const auto delta = subtract(*g.acknowledged, *g.old_acknowledged);
    record_acknowledgement_timing(delta, g, b);
    auto** table = *static_cast<void***>(object);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    reinterpret_cast<engine::MemberFunction<void>>(metadata->original_table[6])(object);
    g.interpolation = misc::query_channel_interpolation(g.engine_client, b.latency);
    if (!changed && player) {
        const auto clock = *g.update_clock;
        if (clock == *g.sequence)
            g.receivers.incoming_complete(player, delta);
    }
    for (auto* patch = g.patches; patch; patch = patch->next)
        if (patch->enabled && patch->target)
            b.exchange(patch->target, patch->replacement);
    if (saved_sequence)
        *const_cast<std::int32_t*>(saved_sequence) = previous_sequence;
}
void bind_incoming_update_runtime(IncomingUpdateRuntime& value) noexcept {
    runtime = &value;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall incoming_update_hook(void* object, void*) {
#else
void incoming_update_hook(void* object, void*) {
#endif
    runtime->process(object);
}
} //namespace skeet::network
