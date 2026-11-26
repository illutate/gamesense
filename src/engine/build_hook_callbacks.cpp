#include "skeet/engine/build_hook_initialization.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/focus_overlay_hooks.hpp"
#include "skeet/animation/fix_animation_backup.hpp"
#include "skeet/misc/ping_spike_channel.hpp"
#include "skeet/events/game_events.hpp"
#include "skeet/render/frame_runtime.hpp"
#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define SKEET_BUILD_CALLER _ReturnAddress()
#define SKEET_BUILD_NOINLINE __declspec(noinline)
#else
#define SKEET_BUILD_CALLER __builtin_return_address(0)
#define SKEET_BUILD_NOINLINE [[gnu::noinline]]
#endif
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_BUILD_HOOK_ABI __fastcall
#else
#define SKEET_BUILD_HOOK_ABI
#endif

namespace skeet::engine {
namespace {
BuildHookCallbacks* active;
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
const VtableHookMetadata& descriptor(void* object) {
    return *static_cast<const VtableHookMetadata*>((*static_cast<void***>(object))[-2]);
}
template <class Result, class... Args>
Result original(void* object, std::size_t slot, Args... args) {
    return reinterpret_cast<MemberFunction<Result, Args...>>(
        descriptor(object).original_table[slot])(object, args...);
}
SKEET_BUILD_NOINLINE std::uintptr_t SKEET_BUILD_HOOK_ABI channel_reset_entry(void* receiver,
                                                                             void*) {
    return active->channel_reset(receiver, reinterpret_cast<std::uintptr_t>(SKEET_BUILD_CALLER));
}
void SKEET_BUILD_HOOK_ABI channel_packet_entry(void* receiver, void*, std::uintptr_t first,
                                               std::uintptr_t second) {
    active->channel_packet(receiver, first, second);
}
void SKEET_BUILD_HOOK_ABI thinkable_detach_entry(void* receiver, void*) {
    active->detach_thinkable(receiver);
}
void SKEET_BUILD_HOOK_ABI local_detach_entry(void* receiver, void*) {
    active->detach_networkable(receiver, true);
}
void SKEET_BUILD_HOOK_ABI remote_detach_entry(void* receiver, void*) {
    active->detach_networkable(receiver, false);
}
void SKEET_BUILD_HOOK_ABI local_frame_entry(void* receiver, void*) {
    active->record_local_frame(receiver);
}
void SKEET_BUILD_HOOK_ABI remote_change_entry(void* receiver, void*, std::int32_t update) {
    active->before_remote_data_change(receiver, update);
}
std::uintptr_t SKEET_BUILD_HOOK_ABI renderable_shadow_entry(void* receiver, void*) {
    return active->renderable_shadow(receiver);
}
std::uintptr_t SKEET_BUILD_HOOK_ABI event_entry(void* receiver, void*, void* event) {
    return active->game_event(receiver, event);
}
std::uintptr_t SKEET_BUILD_HOOK_ABI weapon_entry(void* receiver, void*) {
    return active->weapon_query(receiver);
}
SKEET_BUILD_NOINLINE std::uintptr_t SKEET_BUILD_HOOK_ABI overlay_entry(void* receiver, void*) {
    return active->overlay_stream(receiver, reinterpret_cast<std::uintptr_t>(SKEET_BUILD_CALLER));
}
}

BuildHookCallbacks::BuildHookCallbacks(HookDescriptorStorage& storage, VtableHookRegistry& hooks,
                                       const NativeOffsetStorage& offsets,
                                       animation::FixAnimationsRuntime& animations,
                                       animation::FixAnimationBackupRuntime& backups,
                                       misc::PingSpikeChannelRuntime& channel,
                                       ui::FrameRuntime& frame)
    : data_(storage.image()), offsets_(offsets), animations_(animations), backups_(backups),
      channel_(channel), frame_(frame) {
    if (&backups.detach.hooks != &hooks ||
        &animations.bindings.globals.local_player != &data_.native_cell<void*>(0x43475808) ||
        &channel.bindings.pending_rewind_skips != &data_.native_cell<std::uint32_t>(0x4347C9F4))
        throw std::invalid_argument(
            "Build hooks must share canonical player, channel and cleanup storage");
}

std::uintptr_t BuildHookCallbacks::channel_reset(void* channel, std::uintptr_t caller) { //43416B44
    if (caller == data_.native_cell<std::uintptr_t>(0x43467DE8))
        if (auto* reset = data_.native_cell<std::uint8_t*>(0x43467B24))
            *reset = 0;
    return original<std::uintptr_t>(channel, 22);
}
void BuildHookCallbacks::channel_packet(void* channel, std::uintptr_t first,
                                        std::uintptr_t second) { //4336C1B7
    const auto previous = read<std::uint32_t>(channel, 0x28);
    misc::average_ping_spike_sequences(channel_.bindings.history, 1);
    original<void>(channel, 39, first, second);
    channel_.bindings.pending_rewind_skips |= read<std::uint32_t>(channel, 0x28) ^ previous;
    misc::record_ping_spike_sequences(channel_.bindings.history, 1,
                                      read<std::int32_t>(channel, 0x1C),
                                      read<std::int32_t>(channel, 0x20), channel_.bindings.clock);
}
void BuildHookCallbacks::detach_thinkable(void* receiver) { //43367D1D
    auto* unknown = invoke<void*>(receiver, 0);
    backups_.detach_entity(invoke<void*>(unknown, 7));
    // detach_entity restores this receiver. Native rereads its current table.
    invoke<void>(receiver, 4);
}
void BuildHookCallbacks::detach_networkable(void* receiver, bool local) { //43403563/433FAA6D
    if (local &&
        data_.native_cell<std::int32_t>(0x4346D54C) ==
            *data_.native_cell<const std::int32_t*>(0x434678F8) &&
        data_.native_cell<std::int32_t>(0x4347C9EC) == 2)
        return;
    auto* unknown = invoke<void*>(receiver, 0);
    backups_.detach_entity(invoke<void*>(unknown, 7));
    if (local)
        data_.native_cell<void*>(0x43475808) = nullptr;
    invoke<void>(receiver, 1);
}
void BuildHookCallbacks::record_local_frame(void* receiver) { //433836C8
    if (data_.native_cell<std::int32_t>(0x4347C9EC) == 2)
        data_.native_cell<std::int32_t>(0x4346D54C) =
            *data_.native_cell<const std::int32_t*>(0x434678F8);
    original<void>(receiver, 13);
}
void BuildHookCallbacks::before_remote_data_change(void* receiver, std::int32_t update) { //4339CFA8
    const auto index = invoke<std::uint32_t>(receiver, 10);
    auto& state = animations_.bindings.players.get(index)->native;
    if (update == 1)
        write(&state, 0x1D4, std::uint32_t(0));
    else if (update == 0) {
        const auto time = invoke<float>(data_.native_cell<void*>(0x4346A514), 14);
        const auto interval = read<float>(data_.native_cell<void*>(0x43467B04), 0x20);
        const auto rounded = float(static_cast<long double>(time) / interval + 0.5L);
        const auto tick = _mm_cvttss_si32(_mm_set_ss(rounded));
        auto* player = static_cast<std::byte*>(receiver) - 8;
        write(player, animations_.bindings.fields.animation_frame_74019d87, std::uint32_t(0));
        const auto origin = read<std::array<std::uint32_t, 3>>(&state, 0x1C8);
        write(&state, 0x120, origin);
        write(&state, 0x1A4, tick);
        write(&state, 0x11C, read<std::uint32_t>(invoke<void*>(player, 77), 8));
    }
    if (update == 0 || update == 1)
        write(&state, 0x668, UINT32_MAX);
    original<void>(receiver, 3, update);
}
std::uintptr_t BuildHookCallbacks::renderable_shadow(void* receiver) { //4336F76D
    return data_.native_cell<std::uint8_t>(0x43467AEC) ? 0 : original<std::uintptr_t>(receiver, 37);
}
std::uintptr_t BuildHookCallbacks::game_event(void* receiver, void* event) { //433E9471
    auto* player = data_.native_cell<void*>(0x43475808);
    bool exchanged = false;
    if (event && player && *static_cast<void***>(player) == data_.native_cell<void**>(0x43475964)) {
        const auto* name = invoke<const char*>(event, 1);
        if (name && events::event_hash(name) == events::player_death_hash) {
            std::atomic_ref<void**>(*static_cast<void***>(player))
                .exchange(data_.native_cell<void**>(0x4347595C));
            exchanged = true;
        }
    }
    const auto result = original<std::uintptr_t>(receiver, 1, event);
    if (!exchanged)
        return result;
    // Native leaves the XCHG result in EAX on this path.
    return reinterpret_cast<std::uintptr_t>(std::atomic_ref<void**>(*static_cast<void***>(player))
                                                .exchange(data_.native_cell<void**>(0x43475964)));
}
std::uintptr_t BuildHookCallbacks::weapon_query(void* weapon) { //4334ADEE
    void* player = nullptr;
    void** previous = nullptr;
    const auto owner = read<std::uint32_t>(
        weapon, DecodedOffset(offsets_.globals, 0x434679D8, 0x43467FB4, 0x87907BC3, 0));
    if (owner != UINT32_MAX) {
        auto* entity = invoke<void*>(data_.native_cell<void*>(0x4346A890), 4, owner);
        if (entity) {
            player = invoke<void*>(entity, 161);
            if (player && invoke<bool>(player, 158)) {
                auto** table = *static_cast<void***>(player);
                if (table == data_.native_cell<void**>(0x43475840) ||
                    table == data_.native_cell<void**>(0x43475964)) {
                    previous = table;
                    std::atomic_ref<void**>(*static_cast<void***>(player))
                        .exchange(data_.native_cell<void**>(0x43475838));
                }
            }
        }
    }
    const auto result = original<std::uintptr_t>(weapon, 98);
    if (previous && player)
        std::atomic_ref<void**>(*static_cast<void***>(player)).exchange(previous);
    return result;
}
std::uintptr_t BuildHookCallbacks::overlay_stream(void* receiver,
                                                  std::uintptr_t caller) { //4341F338
    if (caller == data_.native_cell<std::uintptr_t>(0x43468D94))
        frame_.render();
    return original<std::uintptr_t>(receiver, 2);
}
void BuildHookCallbacks::bind_static(std::uint32_t identity, VtableHookMetadata& metadata,
                                     std::uint32_t slot) {
    void* entry;
    switch (identity) {
    case 0x43416B44:
        entry = reinterpret_cast<void*>(&channel_reset_entry);
        break;
    case 0x4336C1B7:
        entry = reinterpret_cast<void*>(&channel_packet_entry);
        break;
    case 0x43367D1D:
        entry = reinterpret_cast<void*>(&thinkable_detach_entry);
        break;
    case 0x43403563:
        entry = reinterpret_cast<void*>(&local_detach_entry);
        break;
    case 0x433FAA6D:
        entry = reinterpret_cast<void*>(&remote_detach_entry);
        break;
    case 0x433836C8:
        entry = reinterpret_cast<void*>(&local_frame_entry);
        break;
    case 0x4339CFA8:
        entry = reinterpret_cast<void*>(&remote_change_entry);
        break;
    case 0x4336F76D:
        entry = reinterpret_cast<void*>(&renderable_shadow_entry);
        break;
    case 0x433E9471:
        entry = reinterpret_cast<void*>(&event_entry);
        break;
    case 0x4334ADEE:
        entry = reinterpret_cast<void*>(&weapon_entry);
        break;
    case 0x43416A0F:
        bind_focus_overlay_first_entry(metadata);
        return;
    case 0x4341F338:
        entry = reinterpret_cast<void*>(&overlay_entry);
        break;
    default:
        throw std::invalid_argument("Unknown build hook callback identity");
    }
    metadata.replacement_table[slot] = entry;
}
void BuildHookCallbacks::bind(std::uint32_t identity, VtableHookMetadata& metadata,
                              std::uint32_t slot) {
    if (identity != 0x43416A0F)
        active = this;
    bind_static(identity, metadata, slot);
}
}
#undef SKEET_BUILD_HOOK_ABI
#undef SKEET_BUILD_CALLER
#undef SKEET_BUILD_NOINLINE
