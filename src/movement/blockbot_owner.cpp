#include "skeet/movement/blockbot_owner.hpp"
#include "skeet/hud/composite_handlers.hpp"
#include "skeet/misc/command_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define BLOCK_MEMBER __thiscall
#define BLOCK_FAST __fastcall
#else
#define BLOCK_MEMBER
#define BLOCK_FAST
#endif
using OwnerEntry = void(BLOCK_MEMBER*)(void*);
void BLOCK_MEMBER destroy(void*);
void BLOCK_MEMBER unchanged(void*) {} //433CCE8E
void BLOCK_MEMBER reset(void*);
void BLOCK_MEMBER forward_reset(void*);
struct HudEndpoint : hud::HudFrameCallback {
    hud::BlockbotHudBinding binding;
};
void BLOCK_FAST draw(void* endpoint, ui::RenderQueue* queue, void* player, std::int32_t* cursor) {
    static_cast<HudEndpoint*>(static_cast<hud::HudFrameCallback*>(endpoint))
        ->binding.draw(*queue, player, *cursor);
}
const std::array<hud::HudFrameEntry, 1> hud_table{draw}; //43472498
const std::array<OwnerEntry, 9> lifecycle_table{//434728AC
                                                destroy,   unchanged,     unchanged,
                                                unchanged, unchanged,     reset,
                                                unchanged, forward_reset, unchanged};
}
class BlockbotOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    BlockbotState& state;
    misc::CommandOwner command;
    client::NativeCommandPhaseReceiver command_receiver;
    HudEndpoint hud_endpoint;
    BlockbotOwner(engine::OwnerRegistryBinding lifecycle, client::CommandPhaseRegistry& commands,
                  engine::OwnerRegistryBinding callbacks, ConfigRegistries& registries,
                  BlockbotConfig& settings, BlockbotState& shared_state,
                  const BlockbotBindings& bindings, const esp::Projection& projection,
                  const std::uint32_t& life_state_offset)
        : memory(lifecycle.memory), state(shared_state),
          command(misc::BlockbotCommandOwner{state, settings.enabled, settings.hotkey, bindings}),
          hud_endpoint{{nullptr}, {state, projection, life_state_offset}} {
        lifecycle.append(this);
        commands.append(command_receiver, &command);
        hud::register_hud_frame(callbacks, &hud_endpoint);
        vtable = lifecycle_table.data();
        commands.publish(command_receiver, client::CommandOwnerId::blockbot); //43480289
        hud_endpoint.vtable = hud_table.data();
        initialize_blockbot_config(registries, settings);
        initialize_blockbot_state(state);
    }
};
namespace {
void BLOCK_MEMBER destroy(void* object) { //433B1A7A tail433837D0
    if (!object)
        return;
    auto* owner = static_cast<BlockbotOwner*>(object);
    const auto memory = owner->memory;
    owner->~BlockbotOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void BLOCK_MEMBER reset(void* object) { //4332057F, preserves direction and upper flag bits
    auto& state = static_cast<BlockbotOwner*>(object)->state;
    if (state.activated)
        state.activated = 0;
    state.target_handle = -1;
    state.flags &= 0xFEu;
}
void BLOCK_MEMBER forward_reset(void* object) { //433CF2F5
    static_cast<BlockbotOwner*>(object)->vtable[5](object);
}
#undef BLOCK_MEMBER
#undef BLOCK_FAST
}
std::size_t blockbot_owner_size() noexcept {
    return sizeof(BlockbotOwner);
}
BlockbotOwner& construct_blockbot_owner(void* storage, engine::OwnerRegistryBinding lifecycle,
                                        client::CommandPhaseRegistry& commands,
                                        engine::OwnerRegistryBinding callbacks,
                                        ConfigRegistries& registries, BlockbotConfig& settings,
                                        BlockbotState& state, const BlockbotBindings& bindings,
                                        const esp::Projection& projection,
                                        const std::uint32_t& life_state_offset) {
    return *::new (storage) BlockbotOwner(lifecycle, commands, callbacks, registries, settings,
                                          state, bindings, projection, life_state_offset);
}
} //namespace skeet
