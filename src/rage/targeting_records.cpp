#include "skeet/rage/targeting.hpp"
#include "skeet/resolver_runtime/attempts.hpp"
#include <cstring>

namespace skeet::rage {
TargetRecordChoice choose_target_record(const TargetRecordHistory& history,
                                        std::int32_t requested_tick, float interval) noexcept {
    if (!history.count)
        return {};
    auto origin = history.origin;
    const auto requested_time = float(requested_tick) * interval;
    TargetRecordChoice result;
    for (std::uint32_t i = 0; i != history.count; ++i) {
        const auto record = history.records[(history.first + i) & 31U];
        const auto x = record->origin[0] - origin[0];
        const auto y = record->origin[1] - origin[1];
        const auto z = record->origin[2] - origin[2];
        if ((y * y + x * x) + z * z > 4096.0f)
            return {};
        origin = record->origin;
        result = {record, i};
        // Native COMISS/JAE continues traversal for unordered values.
        if (requested_time >= float(record->simulation_tick) * interval)
            break;
    }
    return result;
}

TargetRecordCopy prepare_target_record_copy(TargetLagRecordHistory& history,
                                            std::int32_t requested_tick, float interval,
                                            std::uint32_t options, lagcomp::LagRecord& destination,
                                            std::uint32_t& output_flags) noexcept {
    output_flags = 0;
    destination.header.field_00 = 0;
    TargetRecordHistory traversal{history.origin, history.first, history.count, {}};
    for (std::size_t i = 0; i != history.records.size(); ++i)
        if (history.records[i])
            traversal.records[i] = &history.records[i]->header;
    const auto choice = choose_target_record(traversal, requested_tick, interval);
    if (!choice.record)
        return {};
    const auto source = history.records[(history.first + choice.ordinal) & 31U];
    resolver_runtime::advance_pending_attempt(*source, options);
    std::memcpy(&destination, source, sizeof destination);
    destination.header.field_00 = std::uint32_t(reinterpret_cast<std::uintptr_t>(source));
    return {source, choice.ordinal};
}
} // namespace skeet::rage
