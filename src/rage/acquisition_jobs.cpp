#include "skeet/rage/acquisition.hpp"
#include "skeet/rage/preamble.hpp"
#include "skeet/menu/misc_menu.hpp"
#include <cstring>

namespace skeet::rage {
namespace {
struct Work {
    PreambleState* state;
    ScanContext* context;
    ScanRequest* request;
};
static_assert(sizeof(Work) <= sizeof(AcquisitionWorkItem));
#if defined(_M_IX86) || defined(__i386__)
#define ACQUISITION_FASTCALL __fastcall
#else
#define ACQUISITION_FASTCALL
#endif
void ACQUISITION_FASTCALL scan_work(void* item) {
    Work work;
    std::memcpy(&work, item, sizeof(work));
    const auto index = work.request->evaluation.candidate.entity_index & 63U;
    if (!(work.state->previous_targets[index >> 5] & (1U << (index & 31U))))
        scan_candidate(*work.context, *work.request);
}
#undef ACQUISITION_FASTCALL
}
void submit_acquisition_scan(AcquisitionStorage& storage, PreambleState& state,
                             ScanContext& context, void* player, std::uint32_t ordinal,
                             std::uint32_t flags, const ui::SettingsMenuValues& settings_menu) {
    auto& slot = storage.evaluations[ordinal];
    auto& candidate = storage.candidates[ordinal];
    auto& request = storage.requests[ordinal];
    request.emplace(ScanRequest{player,
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(player)),
                                {candidate, slot.record, slot.source_record,
                                 slot.settings.scan_policy(), slot.ballistic_evaluations},
                                flags,
                                state.completed,
                                ordinal});
    request->evaluation.policy.body_disabler_reasons = slot.body_disabler_reasons;
    if (settings_menu.multithreading && storage.queued.inline_storage &&
        storage.queued.count < 80) {
        const Work work{&state, &context, &*request};
        std::memcpy(storage.queued.items[storage.queued.count++].bytes.data(), &work, sizeof(work));
    } else {
        scan_candidate(context, *request);
    }
}
void dispatch_acquisition_scans(AcquisitionStorage& storage, engine::JobDispatcher& jobs,
                                const ui::SettingsMenuValues& settings_menu) {
    if (!settings_menu.multithreading)
        return;
    if (!storage.queued.count)
        return;
    if (storage.queued.count == 1) {
        scan_work(&storage.queued.items[0]);
        storage.queued.clear();
        return;
    }
    auto* task = jobs.create_thirty_six(scan_work);
    jobs.dispatch_thirty_six(*task, storage.queued.items.data(), storage.queued.count);
    engine::release_job(*task);
    storage.queued.clear();
}
void finish_target_acquisition(PreambleState& state, AcquisitionStorage& storage,
                               engine::JobDispatcher& jobs, client::PlayerStateCache& players,
                               std::int32_t maximum_clients,
                               const ui::SettingsMenuValues& settings_menu) {
    dispatch_acquisition_scans(storage, jobs, settings_menu);
    if (state.flags & 9U)
        return;
    for (std::int32_t index = 1; index <= maximum_clients; ++index) {
        auto* player = players.get(std::uint32_t(index));
        const std::uint32_t zero = 0;
        std::memcpy(reinterpret_cast<std::byte*>(&player->native) + 0x194, &zero, sizeof(zero));
    }
}
} // namespace skeet::rage
