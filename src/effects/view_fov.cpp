#include "skeet/effects/view_fov.hpp"
#include "skeet/esp/player_runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <emmintrin.h>

namespace skeet::effects {
namespace {
template <class T> T read(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
// This adapter supplies only actual entity-list/player virtuals to the existing
// complete43359AFA observer resolver; it does not replace its selection logic.
struct LiveViewPlayer {
    ViewFovRuntime& runtime;
    esp::RuntimePlayer local{}, observed{};
    esp::PlayerRuntimeEngine engine{};
    explicit LiveViewPlayer(ViewFovRuntime& r) : runtime(r) {
        const auto player = *r.local_player;
        if (player) {
            local.engine_entity = player;
            local.life_state = read<std::uint8_t>(player, r.offsets.life_state);
            local.move_type = read<std::uint8_t>(player, r.offsets.movement_type);
            local.observer_mode = read<std::int32_t>(player, r.offsets.observer_mode);
            local.observer_handle = read<std::int32_t>(player, r.offsets.observer_target);
        }
        engine.context = this;
        engine.local_player = player ? &local : nullptr;
        engine.by_handle = [](void* context, std::int32_t handle) -> esp::RuntimePlayer* {
            auto& self = *static_cast<LiveViewPlayer*>(context);
            const auto player = skeet::engine::invoke<void*>(self.runtime.entity_list, 4, handle);
            if (!player)
                return nullptr;
            self.observed.engine_entity = player;
            return &self.observed;
        };
        engine.is_player = [](void*, const esp::RuntimePlayer& player) {
            return skeet::engine::invoke<bool>(player.engine_entity, 158);
        };
    }
    void* selected() {
        const auto player = esp::effective_view_player(engine);
        return player ? player->engine_entity : nullptr;
    }
};
}
void ViewFovRuntime::update(float& value) {
    LiveViewPlayer live(*this);
    const auto player = live.selected();
    if (!player || read<std::int32_t>(player, offsets.fov) == 45)
        return;
    const auto base = engine::invoke<std::int32_t>(player, 333);
    if (settings.zoom_fov.value != 100) {
        const auto difference = sub(_mm_cvttss_si32(_mm_set_ss(value)), base);
        if (difference) {
            auto scaled = _mm_cvtepi32_ps(_mm_cvtsi32_si128(settings.zoom_fov.value));
            scaled = _mm_mul_ss(scaled, _mm_set_ss(0.01f));
            scaled = _mm_mul_ss(scaled, _mm_cvtepi32_ps(_mm_cvtsi32_si128(difference)));
            scaled = _mm_add_ss(scaled, _mm_set_ss(0.5f));
            value = static_cast<float>(add(_mm_cvttss_si32(scaled), base));
        }
    }
    const auto difference = sub(settings.fov.value, base);
    if (difference) {
        auto shifted = _mm_set_ss(static_cast<float>(difference) + value);
        shifted = _mm_max_ss(shifted, _mm_set_ss(1.0f));
        shifted = _mm_min_ss(shifted, _mm_set_ss(160.0f));
        value = _mm_cvtss_f32(shifted);
    }
}
} // namespace skeet::effects
