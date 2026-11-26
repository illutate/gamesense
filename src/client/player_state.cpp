#include "skeet/client/player_state.hpp"
#include <bit>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
PlayerState::PlayerState(NativePlayerStateLayout& data)
    : native(data), geometry(data.geometry), cached_target_height(data.cached_target_height),
      cached_definition(data.cached_definition), fallback(data.fallback) {
    history.first = data.history_first;
    history.count = data.history_count;
    for (std::size_t index = 0; index < 32; ++index)
        history.records[index] = reinterpret_cast<lagcomp::LagRecord*>(
            static_cast<std::uintptr_t>(data.native_history_links[index]));
}
resolver_runtime::EnvironmentOwner PlayerState::environment() const noexcept {
    resolver_runtime::EnvironmentOwner result{};
    for (std::size_t index = 0; index < 4; ++index)
        result.selection.attempt_by_mode[index] = native.resolver_slots[index].attempt;
    result.selection.status = native.resolver_status;
    result.probe_time = native.probe_time;
    result.probe_stamp = native.probe_stamp;
    result.probe_count = native.probe_count;
    result.reference_time = native.reference_time;
    result.reference_origin = native.reference_origin;
    result.last_update_tick = native.last_update_tick;
    result.reference_yaw = native.reference_yaw;
    return result;
}
void PlayerState::environment(const resolver_runtime::EnvironmentOwner& value) noexcept {
    for (std::size_t index = 0; index < 4; ++index)
        native.resolver_slots[index].attempt = value.selection.attempt_by_mode[index];
    native.resolver_status = value.selection.status;
    native.probe_time = value.probe_time;
    native.probe_stamp = value.probe_stamp;
    native.probe_count = value.probe_count;
    native.reference_time = value.reference_time;
    native.reference_origin = value.reference_origin;
    native.last_update_tick = value.last_update_tick;
    native.reference_yaw = value.reference_yaw;
}
lagcomp::PlayerCaptureState PlayerState::capture_state() const noexcept {
    lagcomp::PlayerCaptureState result{};
    result.field_180 = native.capture_field_180;
    result.field_1ac = std::bit_cast<std::uint32_t>(native.reference_yaw);
    result.resolver_index = native.resolver_index;
    result.resolver_state = native.resolver_status;
    // Capture consumes exactly the selected byte. The native owner guarantees
    // the resolver index's slot is readable, as at 433FD8F8.
    const auto bytes = reinterpret_cast<const std::uint8_t*>(&native);
    result.resolver_modes[result.resolver_index] =
        bytes[0x10 + std::uint32_t(result.resolver_index) * 0x24];
    return result;
}
void PlayerState::publish_history() noexcept {
    native.history_first = history.first;
    native.history_count = history.count;
    if constexpr (sizeof(void*) == 4)
        for (std::size_t index = 0; index < 32; ++index)
            native.native_history_links[index] = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(history.records[index]));
}
lagcomp::HistoryRecordingState PlayerState::recording_state() const {
    return {history, fallback, native.flags, native.resolver_index};
}
void PlayerState::recording_state(const lagcomp::HistoryRecordingState& value) {
    // This publishes the complete owner projection, including padding cleared
    // by prepare_history_capture's native full-record memset.
    history = value.ring;
    std::memcpy(&fallback, &value.fallback, sizeof fallback);
    native.flags = value.flags;
    native.resolver_index = value.resolver_index;
    publish_history();
}

PlayerStateCache::PlayerStateCache(engine::JobMemory memory)
    : addresses(standalone_addresses_), memory_(memory) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Player cache requires process heap exports");
}
PlayerStateCache::PlayerStateCache(engine::JobMemory memory,
                                   std::array<NativePlayerStateLayout*, 64>& shared)
    : addresses(shared), memory_(memory) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Player cache requires process heap exports");
    for (auto* object : addresses)
        if (object)
            throw std::invalid_argument(
                "Source player cache cannot adopt pre-existing player allocations");
}
PlayerStateCache::~PlayerStateCache() {
    // The process terminal path uses finalize_history with its actual record
    //pool. An independently owned cache releases only still-present raw blocks
    //here; terminal cleanup has already nulled its freed slots.
    for (auto*& object : addresses) {
        free(object);
        object = nullptr;
    }
}
void PlayerStateCache::finalize_history(lagcomp::RecordPool& pool) {
    for (std::uint32_t index = 0; index < addresses.size(); ++index) {
        auto* const player = view(index);
        if (!player)
            continue;
        lagcomp::clear_history_records(player->history, pool);
        player->publish_history();
    }
    pool.finalize_free_records();
    for (std::size_t index = addresses.size(); index;) {
        const auto slot = --index;
        if (!addresses[slot])
            continue;
        free(addresses[slot]);
        addresses[slot] = nullptr;
        views_[slot].reset();
    }
}
NativePlayerStateLayout* PlayerStateCache::allocate() {
    constexpr std::uint32_t size = 0x1700, allocation_size = size + 0x20;
    auto* raw = memory_.allocate(memory_.process_heap, 8, allocation_size);
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    auto* object = ::new (reinterpret_cast<void*>(header + 16)) NativePlayerStateLayout;
    std::memset(object, 0, size);
    object->field_238 = 0;
    object->history_first = 0;
    object->history_count = 0;
    object->fallback.header.field_00 = 0;
    return object;
}
void PlayerStateCache::free(void* object) {
    if (!object)
        return;
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory_.free(memory_.process_heap, 0, bytes - 16 - displacement);
}
PlayerState* PlayerStateCache::view(std::uint32_t slot) {
    const auto pointer = addresses[slot];
    if (!pointer) {
        views_[slot].reset();
        return nullptr;
    }
    auto& existing = views_[slot];
    if (!existing || &existing->native != pointer)
        existing.emplace(*pointer);
    return &*existing;
}
PlayerState* PlayerStateCache::find(std::uint32_t index) {
    return view(index & 63);
}
PlayerState* PlayerStateCache::get(std::uint32_t index) {
    const auto slot = index & 63;
    if (!addresses[slot]) {
        const auto object = allocate();
        if (object != addresses[slot]) {
            free(addresses[slot]);
            addresses[slot] = object;
        }
    }
    return view(slot);
}
} // namespace skeet::client
