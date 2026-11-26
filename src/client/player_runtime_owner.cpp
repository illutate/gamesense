#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include <stdexcept>

namespace skeet::client {
ClientRuntimeStorage& PlayerRuntimeOwner::require_available(ClientRuntimeStorage& storage) {
    if (storage.active_player_owner_)
        throw std::logic_error("The source player cache already has a process owner");
    return storage;
}
PlayerRuntimeOwner::PlayerRuntimeOwner(ClientRuntimeStorage& storage)
    : storage_(require_available(storage)),
      cache_(storage.memory(),
             storage.data().native_cell<std::array<NativePlayerStateLayout*, 64>>(0x434797BCu)),
      adjustments_(storage.data().native_cell<players::State>(0x4347BE04u)) {
    storage_.active_player_owner_ = this;
}
PlayerRuntimeOwner::~PlayerRuntimeOwner() {
    // The registered source terminal pass may already have drained these
    // exact slots. Repeating an empty drain releases no additional objects.
    cache_.finalize_history(records_);
    storage_.active_player_owner_ = nullptr;
}
} //namespace skeet::client
