#include "skeet/esp/user_message_handlers.hpp"
#include "skeet/esp/owner_runtime.hpp"
#include "skeet/esp/player_engine.hpp"
#include "skeet/engine/preserved_initialization.hpp"
#include "detail.hpp"
#include <cstring>
#include <stdexcept>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
const void* pointer32(const void* object, std::ptrdiff_t offset) {
    return reinterpret_cast<const void*>(std::uintptr_t(field<std::uint32_t>(object, offset)));
}
std::int32_t clock_result(float time, float interval) {
    return detail::truncate(
        float(static_cast<long double>(time) / static_cast<long double>(interval) + 0.5L));
}
float integer_float(std::int32_t value) {
    return _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(value)));
}
UserMessageHandlers* handlers;
}

UserMessageHandlers::UserMessageHandlers(engine::PreservedInitialization& initialization,
                                         UserMessageBindings bindings)
    : UserMessageHandlers(initialization.image_owner(), bindings) {}
UserMessageHandlers::UserMessageHandlers(std::shared_ptr<engine::NativeDataImage> image,
                                         UserMessageBindings bindings)
    : image_(image), bindings_(bindings) {
    (void)engine::require_native_data_image(image_);
    if (&bindings_.active_owner != &image_->native_cell<EspOwnerRuntime*>(0x434758dc))
        throw std::invalid_argument("User messages require the canonical active ESP owner cell");
    if (&bindings_.recording_enabled != &image_->native_cell<std::uint8_t>(0x4346754c))
        throw std::invalid_argument("User messages require the canonical recording flag cell");
    const auto* correction =
        bindings_.adjustments.settings_base() + offsetof(players::Adjustments, correction_active);
    if (correction != &image_->native_cell<std::uint8_t>(0x4347be0e))
        throw std::invalid_argument("User messages require the canonical player correction bytes");
}

void UserMessageHandlers::weapon_sound(const void* packet) {
    auto* owner = bindings_.active_owner;
    if (!owner || !owner->player_config().dormant.value)
        return;
    if (!(field<std::uint8_t>(packet, 0x24) & 1))
        return;
    const auto present = field<std::uint32_t>(packet, 0x24);
    if (!(present & 2) || !(present & 4) || !(present & 8))
        return;
    const auto index = field<std::uint32_t>(packet, 8);
    if (index - 1u >= 64u)
        return;
    auto& players = owner->player_engine();
    auto* player = players.by_index(std::bit_cast<std::int32_t>(index));
    if (!player || !players.is_player(*player) || !players.dormant(*player))
        return;
    // These three floats are captured before allocation and the first clock
    // call. Geometry's actual SDK bounds/pose reads follow that clock gate.
    const Vector3 origin{field<float>(packet, 0xc), field<float>(packet, 0x10),
                         field<float>(packet, 0x14)};
    auto& cached = *owner->state().players.cache.get(index);
    const auto time = players.time();
    const auto now = clock_result(time, players.tick_interval());
    if (now <= cached.geometry.last_live_tick)
        return;
    update_player_geometry(cached.geometry, *player, players, origin);
    cached.geometry.alpha = 1.0f;
    const auto refreshed_time = players.time();
    cached.geometry.last_live_tick = clock_result(refreshed_time, players.tick_interval());
}

void UserMessageHandlers::spotted_entry(EspOwnerRuntime& owner, const void* entry) {
    const auto index = field<std::uint32_t>(entry, 8);
    if (index - 1u >= 64u)
        return;
    auto& players = owner.player_engine();
    auto* player = players.by_index(std::bit_cast<std::int32_t>(index));
    if (!player || !players.is_player(*player))
        return;
    if (bindings_.recording_enabled && (field<std::uint32_t>(entry, 0x28) & 0x20) &&
        bindings_.adjustments.records[index].adjustments.correction_active) {
        auto& cached = *owner.state().players.cache.get(index);
        cached.native.shared_rule_time = players.rule_time();
        cached.native.shared_yaw = integer_float(field<std::int32_t>(entry, 0x1c));
        const auto time = players.time();
        const auto interval = players.tick_interval();
        const auto quotient = static_cast<long double>(time) / static_cast<long double>(interval);
        cached.native.flags &= 0xffffff3fu;
        cached.native.shared_yaw_tick = detail::truncate(float(quotient + 0.5L));
    }
    if (!owner.player_config().dormant.value || !players.dormant(*player))
        return;
    // A second native cache lookup follows the shared-yaw branch; callbacks
    // may have replaced the selected low-six-bit record in the meantime.
    auto& cached = *owner.state().players.cache.get(index);
    const auto time = players.time();
    const auto interval = players.tick_interval();
    const auto simulation_interval = players.simulation_tick_interval();
    const auto quotient = static_cast<long double>(time) / static_cast<long double>(interval);
    const auto reciprocal = 1.0f / simulation_interval;
    const auto rounded = float(quotient + 0.5L);
    const auto rounded_age = reciprocal + 0.5f;
    const auto now = detail::truncate(rounded);
    const auto maximum_age = detail::truncate(rounded_age);
    if (detail::sub(now, cached.geometry.last_live_tick) <= maximum_age)
        return;
    const Vector3 origin{integer_float(field<std::int32_t>(entry, 0x10)) * 4.0f,
                         integer_float(field<std::int32_t>(entry, 0x14)) * 4.0f,
                         integer_float(field<std::int32_t>(entry, 0x18)) * 4.0f};
    update_player_geometry(cached.geometry, *player, players, origin);
    cached.geometry.alpha = std::bit_cast<float>(0x3f4ccccdu);
    // Unlike WeaponSound, this path retains its pre-geometry clock sample.
    cached.geometry.last_live_tick = now;
}

void UserMessageHandlers::spotted(const void* packet) {
    auto* owner = bindings_.active_owner;
    if (!owner || (!owner->player_config().dormant.value && !bindings_.recording_enabled))
        return;
    for (std::int32_t index = 0; index < field<std::int32_t>(packet, 0xc); ++index) {
        const auto* entries = pointer32(packet, 8);
        const auto* entry = pointer32(entries, std::ptrdiff_t(index) * 4);
        spotted_entry(*owner, entry);
    }
}

void bind_user_message_handlers(UserMessageHandlers& runtime) noexcept {
    handlers = &runtime;
}
void unbind_user_message_handlers(UserMessageHandlers& runtime) noexcept {
    if (handlers == &runtime)
        handlers = nullptr;
}
extern "C" void __cdecl weapon_sound_user_message_hook(const void* packet) {
    handlers->weapon_sound(packet);
}
extern "C" void __cdecl spotted_user_message_hook(const void* packet) {
    handlers->spotted(packet);
}
} //namespace skeet::esp
