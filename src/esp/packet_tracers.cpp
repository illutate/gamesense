#include "skeet/esp/packet_tracers.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/legit/accuracy.hpp"
#include "skeet/rage/pending_shot_publication.hpp"
#include "skeet/rage/targeting_commit.hpp"
#include <bit>
#include <cmath>
#include <cstring>

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void field(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
bool shotgun_patterns(const void* variable) {
    while (field<const void*>(variable, 0x1c) != variable)
        variable = field<const void*>(variable, 0x1c);
    return (field<std::uint32_t>(variable, 0x30) ^
            std::uint32_t(reinterpret_cast<std::uintptr_t>(variable))) != 0;
}
BallisticQuery packet_bullet(void* weapon_system, std::uint16_t definition) {
    client::CommandBullet bullet{};
    rage::capture_shot_weapon_bullet(weapon_system, definition, bullet);
    // Complete433D0015 construction, copying the seven433CCA5C words.
    BallisticQuery result;
    result.allow_penetration = 1;
    reset_ballistic_query(result);
    result.weapon_id = bullet.weapon_id;
    result.weapon_padding = bullet.padding;
    result.penetration = bullet.penetration;
    result.range = bullet.range;
    result.range_modifier = bullet.range_modifier;
    result.armor_ratio = bullet.armor_ratio;
    result.base_damage = bullet.damage;
    result.reserved = bullet.pellet_count;
    return result;
}
}

void process_esp_packet(const void* packet, PacketTracerState& state, SoundOverlay& sounds,
                        const SoundOverlayContext& sound, const EffectsSettings& settings,
                        const PacketTracerBindings& bindings) {
    update_dormant_radar(packet, bindings.offsets.radar, sound);
    if (!settings.bullet_tracers.value || !state.beams || sound.players.suspended() ||
        sounds.ring_model == -1)
        return;
    const auto& offsets = bindings.offsets;
    // The packet master supplies a valid decoded object. Its field is zero-based.
    const auto index = std::bit_cast<std::int32_t>(
        field<std::uint32_t>(packet, offsets.radar.zero_based_index) + 1u);
    auto* shooter = sound.players.by_index(index);
    if (!shooter)
        return;
    auto* local = sound.players.local();
    if (local == shooter || (local && !dormant_enemy(local, shooter, sound.players)))
        return;

    const auto origin = field<Vector3>(packet, offsets.radar.origin);
    const auto recoil = field<float>(packet, offsets.recoil_index);
    const auto seed = field<std::uint32_t>(packet, offsets.seed);
    const auto mode = field<std::int32_t>(packet, offsets.fire_mode);
    const auto definition = field<std::uint16_t>(packet, offsets.weapon_id);
    const auto inaccuracy = field<float>(packet, offsets.inaccuracy);
    const auto spread = field<float>(packet, offsets.spread);
    const rage::SpreadParameters parameters{definition,
                                            mode,
                                            spread,
                                            inaccuracy,
                                            recoil,
                                            1,
                                            shotgun_patterns(bindings.shotgun_patterns_convar)};
    const auto sample =
        rage::generate_spread_sample(parameters, seed, bindings.spread_tables, bindings.random,
                                     bindings.shotgun_pattern, bindings.trigonometry);
    auto angles = field<Vector3>(packet, offsets.angles);
    angles.z = 0.0f;
    const auto basis = legit::spread_basis({angles.x, angles.y, angles.z}, bindings.trigonometry);
    RageVector direction;
    for (std::size_t component = 0; component < direction.size(); ++component)
        direction[component] = (basis.first[component] * sample.first + basis.center[component]) +
                               basis.second[component] * sample.second;
    const auto squared =
        (direction[0] * direction[0] + direction[1] * direction[1]) + direction[2] * direction[2];
    const auto inverse = 1.0f / (std::sqrt(squared) + 0x1p-23f);
    for (auto& component : direction)
        component *= inverse;

    auto bullet = packet_bullet(bindings.weapon_system, definition);
    anti_aim::TargetTraceFilter native_filter(bindings.trace_filter_vtable, bindings.trace_entities,
                                              0x600400bu, 0x600400bu, 1.0f);
    const auto filter = native_filter.binding();
    rage::trace_target_direction(
        bindings.ballistics,
        std::uint32_t(reinterpret_cast<std::uintptr_t>(shooter->engine_entity)),
        {origin.x, origin.y, origin.z}, direction, filter, bullet);
    const auto endpoint = bullet.trace.end;
    const auto dx = endpoint[0] - origin.x;
    const auto dy = endpoint[1] - origin.y;
    const auto dz = endpoint[2] - origin.z;
    // COMISS/JA rejects ordered squared distances below1000; unordered passes.
    if (1000.0f > (dy * dy + dx * dx) + dz * dz)
        return;

    const auto color = settings.tracer_color.value;
    if (state.count == 3) {
        const auto replacing = (state.first + state.count - 1u) % 3u;
        const auto* wanted = state.recent[replacing];
        auto* beam = field<void*>(state.beams, 4);
        while (beam && beam != wanted)
            beam = field<void*>(beam, 0x2c);
        if (beam)
            field(beam, 0xc8, *bindings.prediction_time);
    }
    PacketBeamDescription description;
    description.start = origin;
    description.end = {endpoint[0], endpoint[1], endpoint[2]};
    description.model_index = sounds.ring_model;
    description.brightness = float(color >> 24);
    description.red = float(color & 255u);
    description.green = float((color >> 8) & 255u);
    description.blue = float((color >> 16) & 255u);
    auto* beam = engine::invoke<void*>(state.beams, 12, &description);
    if (!beam)
        return;
    field(beam, 0xdc, 2.0f);
    const auto expires = *bindings.prediction_time + 2.0f;
    field(beam, 0x34, field<std::uint32_t>(beam, 0x34) & 0xffffbfffu);
    field(beam, 0xc8, expires);
    if (state.count != 3)
        ++state.count;
    state.first = (state.first + 2u) % 3u;
    state.recent[state.first] = beam;
}

PacketTracerHandler::PacketTracerHandler(engine::OwnerRegistryBinding registry,
                                         PacketTracerState& state, SoundOverlay& sounds,
                                         const SoundOverlayContext& sound,
                                         const EffectsSettings& settings,
                                         const PacketTracerBindings& bindings)
    : state_(state), sounds_(sounds), sound_context_(sound), settings_(settings),
      bindings_(bindings) {
    registry.append(static_cast<EspPacketEndpoint*>(this));
    vtable = entries_;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall PacketTracerHandler::update(void* object, const void* packet) {
#else
void PacketTracerHandler::update(void* object, const void* packet) {
#endif
    auto& self = *static_cast<PacketTracerHandler*>(static_cast<EspPacketEndpoint*>(object));
    process_esp_packet(packet, self.state_, self.sounds_, self.sound_context_, self.settings_,
                       self.bindings_);
}

} // namespace skeet::esp
