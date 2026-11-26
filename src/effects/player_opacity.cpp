#include "skeet/effects/player_opacity.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <bit>

namespace skeet::effects {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool convar_nonzero(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return field<std::uint32_t>(object, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
RageVector target_eye(void* player, std::ptrdiff_t duck_offset) {
    const auto* origin = engine::invoke<const RageVector*>(player, 10);
    auto eye = *origin;
    const float duck = field<float>(player, duck_offset), square = duck * duck;
    const float smooth = 1.0f - (square * 3.0f - (square + square) * duck);
    eye[2] = (smooth * 18.0f + 46.0f) + eye[2];
    return eye;
}
}
bool player_inside_active_smoke(void* player, const PlayerOpacityBindings& b) {
    if (!b.smoke_registry)
        return false;
    const auto* target_origin = engine::invoke<const RageVector*>(player, 10);
    void* registry = b.smoke_registry;
    std::int32_t index = 0;
    while (index < field<std::int32_t>(registry, 0xC)) {
        const auto* handles = field<const std::uint32_t*>(registry, 0);
        const auto handle = handles[index];
        if (handle != UINT32_MAX) {
            void* smoke = engine::invoke<void*>(b.entities.entity_list, 4, handle);
            if (smoke && field<std::uint8_t>(smoke, b.offsets.smoke_effect_58ae2423)) {
                const auto elapsed = std::bit_cast<std::int32_t>(
                    std::uint32_t(*b.smoke_clock) -
                    field<std::uint32_t>(smoke, b.offsets.smoke_tick_f267e956));
                const float time = float(elapsed) * b.interval;
                if (time > 0.0f && 18.0f > time) {
                    const auto* smoke_origin = engine::invoke<const RageVector*>(smoke, 10);
                    const float x = (*target_origin)[0] - (*smoke_origin)[0];
                    const float y = (*target_origin)[1] - (*smoke_origin)[1];
                    const float z = (*target_origin)[2] - (*smoke_origin)[2];
                    if (22201.0f > ((y * y + x * x) + z * z))
                        return true;
                }
            }
            registry = b.smoke_registry;
        }
        ++index;
    }
    return false;
}
bool player_near_camera_segment(void* player, const PlayerOpacityBindings& b) {
    void* local = b.entities.local_player;
    if (!local || field<std::uint8_t>(local, b.entities.offsets.life_state))
        return false;
    const auto* absolute = engine::invoke<const RageVector*>(local, 10);
    const auto offset = field<RageVector>(local, b.offsets.view_offset_c246315e);
    const RageVector eye{(*absolute)[0] + offset[0], (*absolute)[1] + offset[1],
                         (*absolute)[2] + offset[2]};
    const auto camera = b.camera_position;
    const auto target = target_eye(player, b.offsets.duck_amount_3309c0cf);
    const RageVector delta{camera[0] - eye[0], camera[1] - eye[1], camera[2] - eye[2]};
    const float length =
        std::sqrt((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2]);
    const float inverse = 1.0f / (length + std::numeric_limits<float>::epsilon());
    const RageVector direction{inverse * delta[0], inverse * delta[1], inverse * delta[2]};
    const float projection =
        ((target[1] - eye[1]) * direction[1] + (target[0] - eye[0]) * direction[0]) +
        (target[2] - eye[2]) * direction[2];
    float distance = std::numeric_limits<float>::max();
    if (!(0.0f > projection) && !(projection > length)) {
        const float x = target[0] - (direction[0] * projection + eye[0]);
        const float y = target[1] - (direction[1] * projection + eye[1]);
        const float z = target[2] - (direction[2] * projection + eye[2]);
        distance = std::sqrt((x * x + y * y) + z * z);
    }
    return 36.0f > distance;
}
std::uint8_t evaluate_player_opacity(void* player, const PlayerOpacityBindings& b) {
    std::uint8_t alpha = field<std::uint8_t>(player, b.offsets.field_af649dac) ? 128 : 255;
    if (b.suspended)
        return alpha;
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(player) + 8, 10);
    if (b.player_adjustments.records[index].adjustments.disable_visuals)
        return alpha;
    const bool local = (field<std::uint8_t>(player, b.offsets.local_player_d070684c) & 1U) != 0;
    const bool teammate = !local && model_teammate(player, b.entities);
    const bool history =
        !local && !teammate && b.settings.shadow.value && convar_nonzero(b.lag_compensation.get());
    const auto pass = player_model_colors(b.settings, local, teammate, history);
    if (!(pass.flags & 8U) && player_inside_active_smoke(player, b))
        return alpha;
    if (pass.flags & 4U) {
        const auto visible = std::uint8_t(pass.visible_color >> 24);
        if (visible < alpha)
            alpha = visible;
    }
    if (engine::invoke<void*>(b.input, 32, std::int32_t(-1)) &&
        player_near_camera_segment(player, b))
        alpha = 60;
    return alpha;
}
void apply_player_opacity(void* player, const PlayerOpacityBindings& b) {
    if (!b.active_effects)
        return;
    const auto alpha = evaluate_player_opacity(player, b);
    engine::invoke<void>(player, 396, std::int32_t(alpha == 255 ? 0 : 4), true);
    engine::invoke<void>(player, 395, alpha);
}
} // namespace skeet::effects
