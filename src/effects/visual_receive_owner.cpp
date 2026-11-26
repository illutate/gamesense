#include "skeet/effects/visual_receive_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::effects {
namespace {
client::ClientRuntimeStorage& checked(client::ClientRuntimeStorage& storage,
                                      client::ConfigurationRuntimeOwner& configuration,
                                      client::PlayerRuntimeOwner& players) {
    if (&configuration.storage() != &storage || &players.storage() != &storage ||
        &players.cache().addresses !=
            &storage.data().native_cell<std::array<client::NativePlayerStateLayout*, 64>>(
                0x434797BCu))
        throw std::invalid_argument(
            "Visual receivers require canonical configuration and player-cache owners");
    return storage;
}
}
VisualReceiveOwner::VisualReceiveOwner(client::ClientRuntimeStorage& storage,
                                       client::ConfigurationRuntimeOwner& configuration,
                                       client::PlayerRuntimeOwner& players)
    : storage_(checked(storage, configuration, players)),
      runtime_(storage_.data(), configuration.effects_settings(), storage_.offsets(),
               players.cache()) {}
} //namespace skeet::effects
