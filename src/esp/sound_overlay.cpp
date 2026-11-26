#include "skeet/esp/sound_overlay.hpp"
#include "skeet/engine/interface_call.hpp"
#include "detail.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::esp {
using engine::invoke;
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void field(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
Vector3 vector(MovementVector value) {
    return {value.x, value.y, value.z};
}
std::int32_t clock_tick(const SoundOverlayContext& c) {
    const auto time = c.players.time();
    return detail::truncate(
        float(static_cast<long double>(time) / static_cast<long double>(c.players.tick_interval()) +
              0.5L));
}

// The four recipient queries are the concrete 4346F198 filter: reliable,
// not initialization, one recipient, actual engine local-player index.
#if defined(_M_IX86) || defined(__i386__)
#define SOUND_MEMBER __fastcall
#define SOUND_EDX , void*
#else
#define SOUND_MEMBER
#define SOUND_EDX
#endif
struct Recipient {
    const void* const* table;
    EnginePlayers* players;
    static void* SOUND_MEMBER destroy(void* self SOUND_EDX, std::uint32_t) {
        return self;
    }
    static bool SOUND_MEMBER reliable(void* SOUND_EDX) {
        return true;
    }
    static bool SOUND_MEMBER initialization(void* SOUND_EDX) {
        return false;
    }
    static std::int32_t SOUND_MEMBER count(void* SOUND_EDX) {
        return 1;
    }
    static std::int32_t SOUND_MEMBER index(void* self SOUND_EDX, std::int32_t) {
        return static_cast<Recipient*>(self)->players->local_index();
    }
};
const void* recipient_table[] = {
    reinterpret_cast<const void*>(&Recipient::destroy),
    reinterpret_cast<const void*>(&Recipient::reliable),
    reinterpret_cast<const void*>(&Recipient::initialization),
    reinterpret_cast<const void*>(&Recipient::count),
    reinterpret_cast<const void*>(&Recipient::index),
};
#undef SOUND_MEMBER
#undef SOUND_EDX

void debug_circle(Vector3 center, std::uint32_t color, float duration, void* overlay) {
    // Complete fixed-angle 433EBD74 -> 433F0D0C path used by SoundESP.
    // Pitch=-90, yaw=roll=0 gives the same native up and negative-right axes.
    constexpr float radians = 0.01745329238474369f;
    const Vector3 up{std::sin(-90.0f * radians), 0.0f, std::cos(-90.0f * radians)};
    const Vector3 negative_right{0.0f, 1.0f, 0.0f};
    auto point = center;
    point.x += up.x * 15.0f;
    point.y += up.y * 15.0f;
    point.z += up.z * 15.0f;
    const auto first = point;
    const auto red = std::int32_t(color & 255u);
    const auto green = std::int32_t((color >> 8) & 255u);
    const auto blue = std::int32_t((color >> 16) & 255u);
    const auto alpha = std::int32_t(color >> 24);
    for (std::int32_t step = 1; step <= 32; ++step) {
        const auto previous = point;
        const auto angle = float(step) * 0.19634954631328583f;
        const auto cosine = std::cos(angle), sine = std::sin(angle);
        point = {(up.x * cosine) * 15.0f + center.x + (negative_right.x * sine) * 15.0f,
                 (up.y * cosine) * 15.0f + center.y + (negative_right.y * sine) * 15.0f,
                 (up.z * cosine) * 15.0f + center.z + (negative_right.z * sine) * 15.0f};
        // The native line branch explicitly replaces configured alpha by255.
        if (overlay)
            invoke<void>(overlay, 20, &previous, &point, red, green, blue, std::int32_t(255), true,
                         duration);
        if (alpha != 0 && step > 1)
            invoke<void>(overlay, 3, &first, &previous, &point, red, green, blue, alpha, true,
                         duration);
    }
}

std::int32_t first_sound(const ActiveSoundTree& tree) {
    auto index = tree.root;
    while (index != -1 && tree.nodes[index].left != -1)
        index = tree.nodes[index].left;
    return index;
}
std::int32_t next_sound(const ActiveSoundTree& tree, std::int32_t index) {
    // Complete 433BFEC6, including its allocated/free-node admission checks.
    if (std::uint32_t(index) >= tree.allocation_count || index > tree.last_allocated ||
        tree.nodes[index].left == index)
        return -1;
    if (tree.nodes[index].right != -1) {
        index = tree.nodes[index].right;
        while (tree.nodes[index].left != -1)
            index = tree.nodes[index].left;
        return index;
    }
    auto parent = tree.nodes[index].parent;
    while (parent != -1 && tree.nodes[parent].right == index) {
        index = parent;
        parent = tree.nodes[index].parent;
    }
    return parent;
}
}

void SoundOverlay::initialize_model(void* tables) {
    void* models = nullptr;
    const auto count = invoke<std::int32_t>(tables, 5);
    for (std::int32_t index = 0; index < count; ++index) {
        auto* table = invoke<void*>(tables, 4, index);
        if (!table)
            continue;
        const auto* name = invoke<const char*>(table, 1);
        if (name && events::event_hash(name) == 0x9fe6b136u) {
            models = table;
            break;
        }
    }
    if (!models)
        return;
    initialize_model_table(models);
}
void SoundOverlay::initialize_model_table(void* models) {
    for (const auto wanted : {0x69a49bb6u, 0xf458a8fau, 0x855fee75u}) {
        std::int32_t index = 0;
        auto strings = invoke<std::int32_t>(models, 3);
        for (; index < strings; strings = invoke<std::int32_t>(models, 3)) {
            const auto* name = invoke<const char*>(models, 9, index);
            if (name && events::event_hash(name) == wanted) {
                ring_model = index;
                return;
            }
            ++index;
        }
        ring_model = -1;
    }
}

void SoundOverlay::synchronize_listeners(bool refresh, const PlayerEspConfig& player,
                                         const WorldEspConfig& world,
                                         std::array<events::EventListener, 3> listeners,
                                         events::EventRegistry& registry) {
    if (!refresh)
        return;
    const std::array<std::uint8_t, 3> requested{std::uint8_t(player.dormant.value),
                                                std::uint8_t(world.bomb.enabled.value),
                                                std::uint8_t(world.bomb.enabled.value)};
    for (std::size_t index = 0; index < listeners.size(); ++index) {
        if (requested[index] == listener_enabled[index])
            continue;
        listener_enabled[index] = requested[index];
        if (requested[index])
            registry.add(listeners[index]);
        else
            registry.remove(listeners[index]);
    }
}

void SoundOverlay::consume(RuntimePlayer* view, const ActiveSound& sound,
                           const SoundOverlayContext& c) {
    auto source = sound.source_index;
    if (source <= 0)
        return;
    RuntimePlayer* player = nullptr;
    if (source <= 64)
        player = c.players.by_index(source);
    else {
        RuntimePlayer* entity = nullptr;
        if ((std::uint32_t(source) & 0xffff0000u) == 0)
            entity = c.players.by_index(source);
        if (entity && !c.players.dormant(*entity)) {
            if (!c.players.is_weapon(*entity))
                return;
            const auto definition = c.players.item_definition(*entity);
            const auto owner = c.players.owner_handle(*entity);
            if (owner == -1 || (definition >= 43 && definition <= 48))
                return;
            player = c.players.by_handle(owner);
        } else {
            for (std::uint32_t index = 0; index < c.pickups.count; ++index) {
                const auto& pickup = c.pickups.entries[index];
                if (std::int32_t(pickup.item_index) != source)
                    continue;
                player = c.players.by_index(pickup.player_index);
                break;
            }
        }
    }
    if (!player || !c.players.is_player(*player) || c.players.eligibility(*player))
        return;
    const auto index = c.players.index(*player);
    auto* cached = c.state.cache.get(std::uint32_t(index));
    if (c.players.resource() && c.players.dormant(*player)) {
        if (!c.config.dormant.value || !dormant_alive(player, c.players))
            return;
    } else if (!c.config.visualize_sounds.value || c.players.life(*player) != 0)
        return;
    if (!dormant_enemy(view, player, c.players) ||
        c.player_adjustments.records[std::uint32_t(index)].adjustments.disable_visuals)
        return;

    MovementTraceFlagGuard guard(c.bindings.trace_flag);
    // Actual native ground probe, including its start offset and thin hull.
    const auto raised_z = sound.origin.z + 10.0f;
    const MovementVector delta{sound.origin.x - sound.origin.x, sound.origin.y - sound.origin.y,
                               (sound.origin.z - 72.0f) - raised_z};
    const auto swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
    const MovementCollisionRay ray{
        {sound.origin.x - 16.0f, sound.origin.y - 16.0f, raised_z + 1.0f},
        delta,
        {16.0f, 16.0f, -1.0f},
        {0.0f, 0.0f, 1.0f},
        false,
        swept};
    MovementCollisionTrace hit{};
    c.bindings.collision.trace_ray(
        ray, 1u,
        {MovementCollisionFilterKind::skip_player,
         std::uint32_t(reinterpret_cast<std::uintptr_t>(player->engine_entity)), 0, 0},
        hit);
    if (hit.all_solid || hit.start_solid || !(hit.fraction < 1.0f))
        return;
    auto position = vector(hit.end);
    if (c.config.dormant.value && c.players.dormant(*player)) {
        if (cached->geometry.last_live_tick < clock_tick(c)) {
            update_player_geometry(cached->geometry, *player, c.players, position);
            cached->geometry.alpha = 1.0f;
        }
    }
    if (!c.config.visualize_sounds.value || !c.frame.activation || c.players.suspended())
        return;
    position.z += 6.0f;
    const auto color = c.config.sound_color.value;
    if (ring_model <= 0) {
        const auto interval = c.players.simulation_tick_interval();
        debug_circle(position, color, interval + interval, c.bindings.debug_overlay);
        return;
    }
    const auto elapsed = c.players.animation_time() - field<float>(&cached->native, 0x158);
    // COMISS/JAE suppresses ordered <=0.5; unordered elapsed values pass.
    if (0.5f >= elapsed)
        return;
    auto count = std::int32_t((color >> 24) / 80u) + 1;
    float delay = 0.0f;
    for (; count != 0; --count) {
        Recipient recipient{recipient_table, &c.players};
        if (c.bindings.temp_entities)
            invoke<void>(c.bindings.temp_entities, 9, &recipient, delay, &position, 12.0f, 800.0f,
                         ring_model, std::int32_t(0), std::int32_t(0), std::int32_t(2), 0.75f, 8.0f,
                         std::int32_t(50), 0.0f, std::int32_t(color & 255u),
                         std::int32_t((color >> 8) & 255u), std::int32_t((color >> 16) & 255u),
                         std::int32_t(color >> 24), std::int32_t(0), std::int32_t(8));
        delay += 0.05f;
    }
    field(&cached->native, 0x1b0, position);
    field(&cached->native, 0x158, c.players.animation_time());
}

void process_sound_overlay(RuntimePlayer* view, SoundOverlay& owner,
                           const ActiveSoundTree* const& source, const SoundOverlayContext& c) {
    if (!view || !source || c.players.game_type() == 6 ||
        (!c.config.visualize_sounds.value && !c.config.dormant.value))
        return;
    auto index = first_sound(*source);
    while (index != -1) {
        owner.consume(view, source->nodes[index].value, c);
        index = next_sound(*source, index);
    }
}

void expire_pickup_observations(PickupObservations& observations, std::int32_t frame,
                                float interval) noexcept {
    const auto age = detail::truncate(1.0f / interval + 0.5f);
    const auto threshold = std::bit_cast<std::int32_t>(std::uint32_t(frame) - std::uint32_t(age));
    while (observations.count != 0) {
        if (threshold < observations.entries[observations.count - 1].frame)
            return;
        --observations.count;
    }
}

SoundHistoryReceiver::SoundHistoryReceiver(client::EntityObserverRegistry& registry,
                                           PickupObservations& observations,
                                           const void* const& globals, const float* const& interval)
    : observations_(observations), globals_(globals), interval_(interval) {
    registry.append(*this); //4348265A /4333BF32; table remains unpublished
}
void SoundHistoryReceiver::process_incoming() {
    const auto interval = *interval_;
    const auto frame = field<std::int32_t>(globals_, 0x1c);
    expire_pickup_observations(observations_, frame, interval);
}

void update_dormant_radar(const void* packet, DormantRadarOffsets offsets,
                          const SoundOverlayContext& c) {
    if (!c.config.dormant.value || !packet)
        return;
    const auto raw_index = field<std::uint32_t>(packet, offsets.zero_based_index);
    const auto index = std::bit_cast<std::int32_t>(raw_index + 1u);
    if (index > 64)
        return;
    auto* player = c.players.by_index(index);
    if (!player || !c.players.is_player(*player) || !c.players.dormant(*player))
        return;
    auto* cached = c.state.cache.get(std::uint32_t(index));
    const auto origin = field<Vector3>(packet, offsets.origin);
    MovementTraceFlagGuard guard(c.bindings.trace_flag);
    const MovementVector delta{origin.x - origin.x, origin.y - origin.y,
                               (origin.z - 72.0f) - origin.z};
    const auto swept = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z != 0.0f;
    const MovementCollisionRay ray{{origin.x - 16.0f, origin.y - 16.0f, origin.z + 24.0f},
                                   delta,
                                   {16.0f, 16.0f, -24.0f},
                                   {0.0f, 0.0f, 24.0f},
                                   false,
                                   swept};
    MovementCollisionTrace hit{};
    trace_movement_world(c.bindings.collision, ray, 0x1400bu, hit, c.bindings.trace_flag);
    if (hit.start_solid || hit.all_solid)
        return;
    const auto position = vector(hit.end);
    const auto dx = position.x - cached->geometry.origin.x;
    const auto dy = position.y - cached->geometry.origin.y;
    const auto dz = position.z - cached->geometry.origin.z;
    if (!((dy * dy + dx * dx) + dz * dz > 16.0f))
        return;
    update_player_geometry(cached->geometry, *player, c.players, position);
    cached->geometry.alpha = 1.0f;
    cached->geometry.last_live_tick = clock_tick(c);
}

} // namespace skeet::esp
