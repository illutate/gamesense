#include "skeet/events/hit_marker.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/core.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::events {
void HitMarker::player_hurt(GameEventReader& event, HitMarkerBindings& bindings,
                            void* local_player) {
    if (!settings.sound.value && !settings.enabled.value)
        return;
    FieldValue attacker, victim;
    if (event.read(0x455B4DBF, FieldType::integer, attacker))
        return;
    if (event.read(0x8361150E, FieldType::integer, victim))
        return;
    const auto local_index = engine::invoke<std::int32_t>(bindings.engine, 12);
    if (engine::invoke<std::int32_t>(bindings.engine, 9, attacker.integer) != local_index)
        return;
    const auto target_index = engine::invoke<std::int32_t>(bindings.engine, 9, victim.integer);
    if (static_cast<std::uint32_t>(target_index) - 1 > 63)
        return;
    const auto target = engine::invoke<void*>(bindings.entity_list, 3, target_index);
    if (!target)
        return;
    const auto enemy = bindings.enemy.capture(local_player);
    if (!rage::target_is_enemy(enemy, target))
        return;
    if (settings.sound.value && !bindings.sound_blocked) {
        auto* surface = bindings.surface;
        if (surface) {
            auto* sources = bindings.sound_players;
            if (sources)
                for (std::size_t index = 0; index < 64 && sources[index]; ++index) {
                    if (sources[index] == local_index) {
                        sources[index] = 0x7FFFFFFF;
                        surface = bindings.surface; //native4337F28C reload after matching write
                        break;
                    }
                }
            engine::invoke<void>(surface, 82, sound.begin);
        }
    }
    if (settings.enabled.value)
        remaining = 0.4f;
}
namespace {
std::uint32_t marker_alpha(float remaining) {
    auto ratio = remaining / 0.4f;
    ratio = ratio > 0 ? ratio : 0;
    ratio = ratio < 0.9f ? ratio : 0.9f;
    const auto rounded = ratio * 255.0f + 8388608.0f;
    return std::bit_cast<std::uint32_t>(rounded) & 255;
}
void emit_marker(ui::RenderQueue& queue, std::int32_t width, std::int32_t height, float scale,
                 std::uint32_t alpha) {
    const auto size = _mm_cvtss_si32(_mm_set_ss(scale * 10.0f));
    const auto half = size / 2;
    const auto add = [](std::int32_t a, std::int32_t b) {
        return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
    };
    const auto sub = [](std::int32_t a, std::int32_t b) {
        return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
    };
    const auto left = sub(width / 2, size), right = add(width / 2, size);
    const auto top = sub(height / 2, size), bottom = add(height / 2, size);
    const auto shadow = (alpha & 0xFE) << 23;
    const auto color = (alpha << 24) | 0xFFFFFF;
    queue.line(
        {add(left, 1), add(top, 1), add(add(left, half), 1), add(add(top, half), 1), shadow});
    queue.line(
        {add(left, 1), add(bottom, 1), add(add(left, half), 1), add(sub(bottom, half), 1), shadow});
    queue.line(
        {add(right, 1), add(top, 1), add(sub(right, half), 1), add(add(top, half), 1), shadow});
    queue.line({add(right, 1), add(bottom, 1), add(sub(right, half), 1), add(sub(bottom, half), 1),
                shadow});
    queue.line({left, top, add(left, half), add(top, half), color});
    queue.line({left, bottom, add(left, half), sub(bottom, half), color});
    queue.line({right, top, sub(right, half), add(top, half), color});
    queue.line({right, bottom, sub(right, half), sub(bottom, half), color});
}
}
void HitMarker::draw(ui::RenderQueue& queue, const HitMarkerView& view) {
    if (!settings.enabled.value) {
        remaining = 0;
        return;
    }
    remaining = maximum_number(0.0f, remaining - view.frame_time);
    if (!view.player ||
        *(static_cast<const std::uint8_t*>(view.player) + view.rejection_byte) != 0 ||
        !(remaining > 0))
        return;
    const auto alpha = marker_alpha(remaining);
    if (!alpha) {
        remaining = 0;
        return;
    }
    emit_marker(queue, view.width, view.height, view.font_scale, alpha);
}
void HitMarker::draw_live(ui::RenderQueue& queue, void* player, void* const& globals,
                          const engine::DecodedOffset& rejection_byte) {
    if (!settings.enabled.value) {
        remaining = 0;
        return;
    }
    float frame_time;
    std::memcpy(&frame_time, static_cast<const std::byte*>(globals) + 0x14, sizeof frame_time);
    remaining = maximum_number(0.0f, remaining - frame_time);
    if (!player)
        return;
    if (*(static_cast<const std::uint8_t*>(player) + std::ptrdiff_t(rejection_byte)) != 0 ||
        !(remaining > 0))
        return;
    const auto alpha = marker_alpha(remaining);
    if (!alpha) {
        remaining = 0;
        return;
    }
    const auto screen = queue.screen();
    const auto scale = queue.fonts->scale;
    emit_marker(queue, screen.x, screen.y, scale, alpha);
}
} // namespace skeet::events
