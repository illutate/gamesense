#include "skeet/esp/hitbox_highlight_engine.hpp"
#include "skeet/esp/owner_runtime.hpp"
#include <bit>
#include <cstring>

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::ptrdiff_t offset(const engine::NativeOffsetStorage& storage, std::uint32_t pointer,
                      std::uint32_t key, std::uint32_t mask) {
    const auto& globals = storage.globals;
    return static_cast<std::ptrdiff_t>(*globals.pointers.at(pointer) ^ globals.keys.at(key) ^ mask);
}
bool eligible(const RuntimePlayer* target, const HitboxHighlightEngineContext& c) {
    // Complete live43358962. In particular the local entity and game ConVars
    // are read only after target engine queries, rather than borrowed from a
    // previously assembled TargetBindings snapshot.
    auto& players = c.players.get();
    if (!target || !players.is_player(*target) || players.life(*target) ||
        players.health(*target) <= 0 || players.dormant(*target) || players.suppressed(*target) ||
        players.eligibility(*target))
        return false;
    const auto index = players.index(*target);
    if (c.player_adjustments.records[std::uint32_t(index)].adjustments.whitelist)
        return false;
    if (field<std::uint8_t>(target->engine_entity,
                            offset(c.offsets, 0x43467dd0, 0x4346a610, 0xaf649dac)))
        return false;
    return live_enemy(players.local(), target, players);
}
}

EnginePlayers& HitboxHighlightPlayers::get() const {
    if (const auto* fixture = std::get_if<std::reference_wrapper<EnginePlayers>>(&source_))
        return fixture->get();
    auto* owner = std::get<std::reference_wrapper<EspOwnerRuntime* const>>(source_).get();
    if (!owner)
        throw std::logic_error("RAGE highlight requires the actual published ESP player owner");
    return owner->player_engine();
}

std::uint8_t HitboxHighlightEngineContext::life_state(const void* local) const {
    return field<std::uint8_t>(local, offset(offsets, 0x43468e78, 0x434684e4, 0x9d920e35));
}

void draw_live_hitbox_highlight(ui::RenderQueue& queue, const AimbotVisualizationConfig& config,
                                HitboxHighlightState& state, void* local,
                                const HitboxHighlightEngineContext& c) {
    if (!c.active)
        return;
    if (c.life_state(local)) {
        state.target_handle = -1;
        return;
    }
    if (!config.enabled.value && !config.safe_point.value)
        return;
    if (state.target_handle == -1)
        return;
    auto& players = c.players.get();
    auto* target = players.by_handle(state.target_handle);
    if (!eligible(target, c)) {
        state.target_handle = -1;
        return;
    }
    const auto movement = players.movement(*target);
    if (movement == 8 || movement == 10 ||
        (field<std::uint8_t>(target->engine_entity,
                             offset(c.offsets, 0x434688d0, 0x43468c24, 0xe2fbe695)) &
         0x40u)) {
        state.target_handle = -1;
        return;
    }
    const auto* wrapper = field<const void*>(target->engine_entity,
                                             offset(c.offsets, 0x43467e8c, 0x43467900, 0x32e4daf1));
    if (!wrapper)
        return;
    const auto* header = field<const std::byte*>(wrapper, 0);
    if (!header)
        return;
    const auto set_index = field<std::uint32_t>(
        target->engine_entity, offset(c.offsets, 0x43468240, 0x4346a84c, 0xc2b6fb10));
    const auto relative =
        std::bit_cast<std::int32_t>(set_index * 12u + field<std::uint32_t>(header, 0xb0));
    const auto* set = header + relative;
    if (!set || field<std::int32_t>(set, 4) == 0)
        return;
    for (const auto& entry : state.entries) {
        if (!entry.flags)
            continue;
        if (field<std::int32_t>(set, 4) <= entry.hitbox)
            return;
        const auto descriptor_offset = std::bit_cast<std::int32_t>(
            std::uint32_t(entry.hitbox) * 0x44u + field<std::uint32_t>(set, 8));
        const auto* descriptor = set + descriptor_offset;
        const auto radius = field<float>(descriptor, 0x30);
        if (!(radius > 0.0f))
            return; //433D146B COMISS / JBE rejects unordered too
        // The three imported trig pairs precede the live bone-cache lookup.
        // Keep that ordering rather than constructing a target/model snapshot.
        constexpr float radians = 0.01745329238474369f;
        const auto yaw = field<float>(descriptor, 0x28) * radians;
        const auto sy = c.trigonometry.sine(yaw), cy = c.trigonometry.cosine(yaw);
        const auto pitch = field<float>(descriptor, 0x24) * radians;
        const auto sp = c.trigonometry.sine(pitch), cp = c.trigonometry.cosine(pitch);
        const auto roll = field<float>(descriptor, 0x2c) * radians;
        const auto sr = c.trigonometry.sine(roll), cr = c.trigonometry.cosine(roll);
        const auto crcy = cr * cy, crsy = cr * sy, srcy = sr * cy, srsy = sr * sy;
        const Matrix3x4 rotation{{{cp * cy, srcy * sp - crsy, crcy * sp + srsy, 0.0f},
                                  {cp * sy, srsy * sp + crcy, crsy * sp - srcy, 0.0f},
                                  {-sp, sr * cp, cr * cp, 0.0f}}};
        const auto* cache = field<const Matrix3x4*>(
            target->engine_entity, offset(c.offsets, 0x43469064, 0x4346a484, 0x17978090));
        const auto& bone = cache[field<std::uint32_t>(descriptor, 0)];
        Matrix3x4 transform;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 4; ++column) {
                const auto combined = (bone.rows[row][2] * rotation.rows[2][column] +
                                       bone.rows[row][1] * rotation.rows[1][column]) +
                                      bone.rows[row][0] * rotation.rows[0][column];
                transform.rows[row][column] = combined + (column == 3 ? bone.rows[row][3] : 0.0f);
            }
        // Native copies the descriptor only after composing the matrix.
        const auto minimum = field<RageVector>(descriptor, 8);
        const auto maximum = field<RageVector>(descriptor, 0x14);
        RageCapsule capsule{{}, {}, field<float>(descriptor, 0x30)};
        for (unsigned row = 0; row < 3; ++row) {
            const auto& matrix = transform.rows[row];
            capsule.first[row] =
                ((minimum[1] * matrix[1] + matrix[0] * minimum[0]) + matrix[2] * minimum[2]) +
                matrix[3];
            const auto pair = row == 2 ? matrix[1] * maximum[1] + matrix[0] * maximum[0]
                                       : maximum[0] * matrix[0] + maximum[1] * matrix[1];
            capsule.second[row] = (pair + matrix[2] * maximum[2]) + matrix[3];
        }
        draw_capsule_highlight(queue, c.projection, c.view_origin, capsule,
                               (entry.flags & 2u) ? config.safe_point_color.value
                                                  : config.color.value,
                               c.trigonometry);
    }
}
} //namespace skeet::esp
