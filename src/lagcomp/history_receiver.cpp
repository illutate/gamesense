#include "skeet/lagcomp/history_receiver.hpp"

namespace skeet::lagcomp {
HistoryRecordingReceiver::HistoryRecordingReceiver(HistoryRecordingRuntime& value) noexcept
    : runtime(&value), scratch(nullptr) {}
HistoryRecordingReceiver::HistoryRecordingReceiver(HistoryRecordingRuntime& value,
                                                   HistoryRecordingScratch& incoming) noexcept
    : runtime(&value), scratch(&incoming) {}
void HistoryRecordingReceiver::record_players(void* local) const {
    if (scratch)
        runtime->record_players(local, *scratch);
    else
        runtime->record_players(local);
}
} //namespace skeet::lagcomp
