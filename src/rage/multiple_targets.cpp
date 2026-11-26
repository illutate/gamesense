#include "skeet/rage/multiple_targets.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
void clear_target_bit(std::array<std::uint32_t, 2>& words, std::uint32_t entity) noexcept {
    const auto index = entity & 63U;
    words[index >> 5] &= ~(1U << (index & 31U));
}
}
void acquire_multiple_targets(PreambleState& state, PreambleResult& preamble,
                              AcquisitionStorage& storage, const MultipleTargetBindings& bindings) {
    auto& frame = *preamble.frame;
    const auto& preparation = bindings.preparation;
    auto& players = bindings.records.players;
    const TargetView view{frame.origin_dc, field<RageVector>(&frame, 0x1A8),
                          preparation.weapon_settings.maximum_fov.value,
                          std::bit_cast<float>(preamble.scan_template[7])};
    for (std::int32_t entity = std::int32_t(preamble.first_entity);
         entity <= bindings.maximum_clients; ++entity) {
        auto* cached = players.get(std::uint32_t(entity));
        auto* target = engine::invoke<void*>(preparation.targeting.entity_list, 3, entity);
        if (!(state.flags & 9U))
            clear_target_bit(state.target_mask, std::uint32_t(entity));
        if (!target_is_eligible(preparation.targeting, target))
            continue;
        if (!target_passes_scan_filter(preparation.targeting, target, view))
            continue;
        AcquisitionTargetSettings settings{preamble.scan_template};
        const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(target) + 8, 10);
        prepare_acquisition_target_settings(frame, target, *cached, index,
                                            bindings.adjustments.records[index].adjustments,
                                            settings, preparation);
        if (!(state.flags & 9U))
            clear_target_bit(state.previous_targets, std::uint32_t(entity));
        auto body = bindings.body_probe;
        body.context_flags = state.flags;
        body.weapon_flags = state.weapon_flags;
        body.weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
        probe_body_aim(frame, settings, target, body);
        const TargetPredictionBindings prediction{body,
                                                  bindings.prediction.record_selection,
                                                  bindings.prediction.engine_trace,
                                                  bindings.prediction.local_maximum_speed,
                                                  bindings.prediction.scheduler_flags,
                                                  bindings.prediction.command};
        refine_acquisition_minimum_damage(frame, target, *cached, settings, prediction,
                                          bindings.records.bone_runtime);
        auto ticks = acquisition_target_ticks(frame, target, *cached, settings, preparation,
                                              bindings.record_candidates);
        prepare_and_submit_target_records(state, preamble, storage, target, *cached, ticks.values,
                                          settings, ticks.descriptor_flags, bindings.records);
    }
    finish_target_acquisition(state, storage, bindings.jobs, players, bindings.maximum_clients,
                              bindings.records.settings_menu);
}
} // namespace skeet::rage
