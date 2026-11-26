#include "skeet/rage/prediction_masks.hpp"
#include "skeet/anti_aim/targets.hpp"
#include "skeet/esp/player_engine.hpp"
#include "skeet/esp/owner_runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <immintrin.h>
#include <stdexcept>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
bool incoming_shot_has_no_damage(void* shooter, const BallisticVector& origin,
                                 const BallisticVector& destination, PredictionMaskPlayers players,
                                 BallisticEngine& ballistics, void* const* trace_filter_vtable,
                                 const anti_aim::TargetTraceEntities& trace_entities) {
    auto& source = players.get();
    const auto metadata = esp::player_weapon_metadata(*source.identity(shooter), source);
    BallisticQuery query;
    query.allow_penetration = 1;
    reset_ballistic_query(query); //433D0015 ->433FCFD4
    query.weapon_id = metadata.definition;
    query.weapon_padding = metadata.padding;
    query.penetration = metadata.penetration;
    query.range = metadata.range;
    query.range_modifier = metadata.range_modifier;
    query.armor_ratio = metadata.armor_ratio;
    query.base_damage = metadata.damage;
    query.reserved = static_cast<std::uint32_t>(metadata.kind);
    anti_aim::TargetTraceFilter filter(trace_filter_vtable, trace_entities, 0x600400Bu, 0x600400Bu,
                                       1.0f);
    trace_ballistic_bullet(ballistics,
                           static_cast<BallisticEntity>(reinterpret_cast<std::uintptr_t>(shooter)),
                           origin, destination, filter.binding(), query);
    // Native CVTTSS2SI: fractional damage below1 sets the bit; NaN does not.
    return _mm_cvtt_ss2si(_mm_set_ss(query.damage)) == 0;
}
void assign_bit(PredictionMaskState& masks, bool blocked, std::uint32_t index, bool set) {
    const auto bit = 1u << (index & 31u);
    auto& word = index == 64 ? (blocked ? masks.trailing_word : masks.blocked[0])
                             : (blocked ? masks.blocked[index >> 5] : masks.allowed[index >> 5]);
    word = set ? word | bit : word & ~bit;
}
BallisticVector local_probe(void* player, float height) {
    const auto* origin = engine::invoke<const float*>(player, 10);
    return {origin[0] + 0.0f, origin[1] + 0.0f, origin[2] + height};
}
}

esp::EnginePlayers& PredictionMaskPlayers::get() const {
    if (const auto* value = std::get_if<std::reference_wrapper<esp::EnginePlayers>>(&source_))
        return value->get();
    auto* owner = std::get<std::reference_wrapper<esp::EspOwnerRuntime* const>>(source_).get();
    if (!owner)
        throw std::logic_error(
            "Prediction-mask weapon metadata requires the actual published ESP owner");
    return owner->player_engine();
}

void reset_prediction_masks(PredictionMaskState& masks) noexcept {
    masks.allowed.fill(0);
    masks.blocked.fill(0);
}
namespace {
template <class Limit>
void update_ready_masks(PredictionMaskState& masks, const TargetBindings& targeting,
                        const std::uint8_t& enabled, const std::uint32_t& flags, Limit entity_limit,
                        PredictionMaskPlayers players, BallisticEngine& ballistics,
                        void* const* trace_filter_vtable,
                        const anti_aim::TargetTraceEntities& trace_entities) {
    auto* local = targeting.local_player;
    const auto& offsets = targeting.offsets;
    if (!local || field<std::uint8_t>(local, offsets.rejection_bytes[0]) || !enabled ||
        !(flags & 0x20u)) {
        reset_prediction_masks(masks);
        return;
    }
    // The initial origin getter is observable even when the entity limit is0.
    engine::invoke<const float*>(local, 10);
    const auto limit = entity_limit();
    if (limit > 64)
        throw std::out_of_range(
            "Prediction-mask engine limit exceeds the recovered64-player object contract");
    for (std::int32_t index = 1; index <= limit; ++index) {
        auto* target = engine::invoke<void*>(targeting.entity_list, 3, index);
        if (!target_is_eligible(targeting, target)) {
            assign_bit(masks, false, index, false);
            assign_bit(masks, true, index, false);
            continue;
        }
        if (!(field<std::uint8_t>(target, offsets.movement_flags) & 1u))
            continue;
        const auto origin = field<BallisticVector>(target, offsets.origin);
        const BallisticVector start{origin[0] + 0.0f, origin[1] + 0.0f, origin[2] + 64.0f};
        assign_bit(masks, true, index,
                   incoming_shot_has_no_damage(target, start, local_probe(local, 48.0f), players,
                                               ballistics, trace_filter_vtable, trace_entities));
        assign_bit(masks, false, index,
                   incoming_shot_has_no_damage(target, start, local_probe(local, 68.0f), players,
                                               ballistics, trace_filter_vtable, trace_entities));
    }
}
}
void update_prediction_masks(PredictionMaskState& masks, std::uint8_t update,
                             const PredictionMaskBindings& b) {
    if (!engine::invoke<bool>(b.engine_client, 26) || !update)
        return;
    update_ready_masks(
        masks, b.targeting, b.enabled, b.context_flags, [&] { return b.entity_limit; }, b.players,
        b.ballistics, b.trace_filter_vtable, b.trace_entities);
}
void update_prediction_masks(PredictionMaskState& masks, std::uint8_t update,
                             const LivePredictionMaskBindings& b) {
    if (!engine::invoke<bool>(b.engine_client, 26) || !update)
        return;
    const auto targeting = b.targeting.capture(b.local_player);
    update_ready_masks(
        masks, targeting, b.enabled, b.context_flags, [&] { return *b.entity_limit; },
        PredictionMaskPlayers(b.players), b.ballistics, b.trace_filter_vtable, b.trace_entities);
}
void PredictionMaskRuntimeBinding::update(PredictionMaskState& masks, std::uint8_t update) const {
    std::visit([&](auto selected) { update_prediction_masks(masks, update, selected.get()); },
               source_);
}
}
