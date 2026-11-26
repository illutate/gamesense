#include "skeet/lagcomp/update_capture.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include <cstring>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
}
void capture_history_display_metadata(client::PlayerState& owner, void* player,
                                      const EntityStateRuntime& runtime,
                                      const HistoryPreludeOffsets& offsets) {
    owner.geometry.last_live_tick = sample_receive_tick(runtime.engine, runtime.receive_interval);
    if (read<std::uint8_t>(player, offsets.life_state_9d920e35))
        return;
    const auto handle = read<std::uint32_t>(player, runtime.offsets.weapon_handle);
    if (handle == UINT32_MAX)
        return;
    if (auto* weapon = engine::invoke<void*>(runtime.entity_list, 4, handle))
        owner.cached_definition = read<std::uint16_t>(weapon, offsets.weapon_definition_5c9f0fbb);
}
void cache_history_entity_fields(client::PlayerState& owner, void* player,
                                 const EntityStateRuntime& runtime) {
    write(&owner.native, 0x13C, read<Vector>(player, runtime.offsets.origin));
    auto* collideable = engine::invoke<void*>(player, 3);
    owner.cached_target_height = (*engine::invoke<const Vector*>(collideable, 2))[2];
    write(&owner.native, 0x134, read<float>(player, runtime.offsets.duck_amount));
    write(&owner.native, 0x138, read<std::uint32_t>(player, runtime.offsets.field_fb7e3404));
}
CachedHistoryFrame prepare_cached_history_frame(client::PlayerState& owner, RecordPool& pool,
                                                void* player, bool is_local,
                                                const EntityStateRuntime& runtime,
                                                const HistoryPreludeOffsets& offsets) {
    cache_history_entity_fields(owner, player, runtime);
    if (read<std::uint8_t>(player, offsets.life_state_9d920e35) ||
        read<std::uint8_t>(player, offsets.suppressed_cde290d2)) {
        owner.native.field_238 = 0;
        write(&owner.native, 0x234, std::uint32_t(0));
        clear_history_records(owner.history, pool);
        owner.publish_history();
        return {};
    }
    const auto receive = sample_receive_tick(runtime.engine, runtime.receive_interval);
    if (!is_local) {
        const SameTickHistoryState saved{
            read<std::int32_t>(&owner.native, 0x1E4), read<std::int32_t>(&owner.native, 0x1E0),
            read<std::int32_t>(&owner.native, 0x1DC), read<Vector>(&owner.native, 0x1C8)};
        float history = read<float>(&owner.native, 0x110),
              simulation = read<float>(player, runtime.offsets.simulation_time);
        auto velocity = read<Vector>(player, runtime.offsets.velocity);
        const auto writes = restore_same_tick_history_fields(saved, receive, runtime.interval,
                                                             history, simulation, velocity);
        if (writes.simulation) {
            write(&owner.native, 0x110, history);
            write(player, runtime.offsets.simulation_time, simulation);
        }
        if (writes.velocity)
            write(player, runtime.offsets.velocity, velocity);
        capture_history_display_metadata(owner, player, runtime, offsets);
    }
    auto* animation = read<void*>(player, runtime.offsets.animation_state);
    const float fraction = animation ? read<float>(animation, 0x11C) : 1.0f;
    const auto ticks = simulation_tick_span(read<float>(player, offsets.previous_time_1599541f),
                                            read<float>(player, runtime.offsets.simulation_time),
                                            runtime.interval);
    return {true, receive, animation, fraction, ticks};
}
} // namespace skeet::lagcomp
