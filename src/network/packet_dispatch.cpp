#include "skeet/network/packet_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/offset_storage.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/esp/packet_tracers.hpp"
#include "skeet/prediction/history.hpp"
#include <bit>
#include <cstring>

namespace skeet::network {
namespace {
PacketDispatchRuntime* active_runtime = nullptr;
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
void original_update(void* receiver, std::int32_t update_type) {
    const auto table = *static_cast<void***>(receiver);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    using Original = engine::MemberFunction<void, std::int32_t>;
    reinterpret_cast<Original>(metadata->original_table[7])(receiver, update_type);
}
}
PacketDispatchOffsets packet_dispatch_offsets(const engine::NativeOffsetStorage& storage) {
    const auto decode = [&](std::uint32_t pointer, std::uint32_t key, std::uint32_t mask) {
        return engine::DecodedOffset(storage.globals, pointer, key, mask, 0);
    };
    return {decode(0x43467ec0, 0x434678bc, 0xb6f85c08), decode(0x43468490, 0x43467290, 0xb3832000),
            decode(0x4346a60c, 0x434678a8, 0xca5ec698)};
}
void dispatch_packet_update(PacketDispatchRuntime& runtime, void* receiver,
                            std::int32_t update_type) {
    auto* packet = receiver ? static_cast<std::byte*>(receiver) - 4 : nullptr;
    if (engine::invoke<bool>(runtime.engine, 82) || engine::invoke<bool>(runtime.engine, 93)) {
        original_update(receiver, update_type);
        return;
    }
    const auto captured = runtime.callbacks.entries();
    for (auto* object : captured) {
        auto* endpoint = static_cast<esp::EspPacketEndpoint*>(object);
        endpoint->vtable[0](endpoint, packet);
    }
    const auto player = read<std::uint32_t>(packet, runtime.offsets.zero_based_player);
    const auto one_based = std::bit_cast<std::int32_t>(player + 1u);
    if (one_based != engine::invoke<std::int32_t>(runtime.engine, 12)) {
        original_update(receiver, update_type);
        return;
    }
    // The native captures this address before the engine's command-number
    // query, but reads the pointed-to handle only after the history lookup.
    const auto* weapon_cell = packet + runtime.offsets.weapon_identity;
    const auto command = engine::invoke<std::int32_t>(runtime.engine, 205);
    const auto history = prediction::CommandHistoryView::from_owner(runtime.prediction_owner);
    const auto* saved = history.find(command);
    if (!saved)
        return;
    const auto handle = read<std::uint32_t>(weapon_cell, 0);
    if (saved->weapon_identity != handle || handle == UINT32_MAX)
        return;
    auto* weapon = engine::invoke<void*>(runtime.entity_list, 4, handle);
    if (!weapon)
        return;
    if (read<std::int32_t>(saved, 0x5c) <=
        read<std::int32_t>(weapon, runtime.offsets.weapon_ammunition))
        return;
    original_update(receiver, update_type);
}
void bind_packet_dispatch_runtime(PacketDispatchRuntime& runtime) noexcept {
    active_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall packet_update_hook(void* receiver, void*, std::int32_t update_type) {
#else
void packet_update_hook(void* receiver, void*, std::int32_t update_type) {
#endif
    dispatch_packet_update(*active_runtime, receiver, update_type);
}
} // namespace skeet::network
