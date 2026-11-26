#include "skeet/effects/camera.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/client/player_state.hpp"
#include <bit>
#include <cstring>
#include <new>

namespace skeet::effects {
using engine::invoke;
SavedCameraStateView CameraHistory::at(std::uint32_t index) {
    if (source_) {
        auto* player = source_->get(index);
        if (!player)
            throw std::bad_alloc();
        return {player->native.captured_camera_fov, player->native.captured_observer_mode};
    }
    auto& saved = players[index & 63];
    return {saved.fov, saved.observer_mode};
}
namespace {
template <class Value> Value read(void* object, std::uint32_t offset) {
    Value result;
    std::memcpy(&result, static_cast<std::byte*>(object) + offset, sizeof(result));
    return result;
}
void write(void* object, std::uint32_t offset, std::int32_t value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
}

bool CameraRuntime::alive_or_death_camera(void* player) const { // 0x43386EB3
    if (read<std::uint8_t>(player, offsets.life_state) == 0)
        return true;
    const auto mode = read<std::int32_t>(player, offsets.observer_mode);
    return mode == 1 || mode == 2;
}
void CameraRuntime::alive_changed(bool enabled) { // 0x43322683..0x43322743
    if (!invoke<bool>(interfaces.engine, 26))
        return;
    auto* player = interfaces.local_player;
    if (!player)
        return;
    const auto third_person =
        alive_or_death_camera(player) && enabled && hotkey_active(alive_hotkey.binding, keys);
    invoke<void>(player, 312, third_person); // x86 vtable +0x4E0
    if (invoke<void*>(interfaces.input, 32, std::int32_t(-1)))
        write(interfaces.input, 0x100, 2); // re-read global interface after its call
}
void CameraRuntime::dead_changed() { // 0x433662E6..0x43366304
    if (!invoke<bool>(interfaces.engine, 26))
        return;
    if (auto* player = interfaces.local_player)
        update_observer(player);
}
void CameraRuntime::apply_observer_mode(void* player, std::int32_t mode) {
    if (auto proxy = interfaces.observer_proxy) {
        ObserverProxyData data;
        data.words[2] = std::bit_cast<std::uint32_t>(mode);
        data.words[5] = 1;
        proxy(&data, player, static_cast<std::byte*>(player) + offsets.observer_mode);
    }
}
void CameraRuntime::reset_camera(void* player) {
    if (invoke<void*>(interfaces.input, 32, std::int32_t(-1)))
        invoke<void>(interfaces.input, 36); // x86 vtable +0x90
    invoke<void>(player, 312, false);
}
void CameraRuntime::update_observer(void* player) { // 0x43393F51..0x433942FB
    if (!player)
        return;
    if (force_dead != 0 && !alive_or_death_camera(player) &&
        read<std::int32_t>(player, offsets.observer_mode) != 0) {
        const auto handle = read<std::uint32_t>(player, offsets.observer_target);
        if (handle != UINT32_MAX) {
            auto* target = invoke<void*>(interfaces.entity_list, 4, handle);
            if (target && invoke<bool>(target, 158)) {
                write(target, offsets.reset_field_3f51aa0a, 0);
                write(target, offsets.fov, 0);
            }
        }
        write(player, offsets.reset_field_3f51aa0a, 0);
        write(player, offsets.fov, 0);
        if (read<std::int32_t>(player, offsets.observer_mode) == 4) {
            apply_observer_mode(player, 5);
            reset_camera(player);
        }
        forced_observer = true;
        return;
    }
    if (!forced_observer)
        return;
    const auto handle = read<std::uint32_t>(player, offsets.observer_target);
    if (handle != UINT32_MAX) {
        auto* target = invoke<void*>(interfaces.entity_list, 4, handle);
        if (target && invoke<bool>(target, 158)) {
            // The native predicate can change the handle, so load it again.
            const auto current = read<std::uint32_t>(player, offsets.observer_target);
            const auto index = current == UINT32_MAX ? 0x1FFFu : std::uint16_t(current);
            write(target, offsets.fov, history.at(index).fov);
        }
    }
    const auto index = invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    const auto& saved = history.at(index);
    write(player, offsets.fov, saved.fov);
    apply_observer_mode(player, saved.observer_mode);
    reset_camera(player);
    forced_observer = false;
}

} // namespace skeet::effects
