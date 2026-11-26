#include "skeet/misc/rebuy_fix_owner.hpp"
#include "skeet/misc/runtime.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <algorithm>
#include <memory>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_REBUY_ENTRY __fastcall
#else
#define SKEET_REBUY_ENTRY
#endif
void SKEET_REBUY_ENTRY cleanup(void* receiver, void*) {
    static_cast<RebuyPrimaryEndpoint*>(receiver)->owner->destroy();
}
void SKEET_REBUY_ENTRY empty(void*, void*) {}
void SKEET_REBUY_ENTRY frame_update(void* receiver, std::uint8_t notify) {
    static_cast<RebuyFrameEndpoint*>(receiver)->owner->frame_update(notify);
}
#undef SKEET_REBUY_ENTRY
const std::array<void*, 9> primary_table{
    {reinterpret_cast<void*>(&cleanup), reinterpret_cast<void*>(&empty),
     reinterpret_cast<void*>(&empty), reinterpret_cast<void*>(&empty),
     reinterpret_cast<void*>(&empty), reinterpret_cast<void*>(&empty),
     reinterpret_cast<void*>(&empty), reinterpret_cast<void*>(&empty),
     reinterpret_cast<void*>(&empty)}};
const std::array<engine::FrameUpdateEntry, 1> frame_table{{frame_update}};
}
RebuyFixOwner::RebuyFixOwner(RebuyFixOwnerBindings bindings)
    : bindings_(bindings), command_owner_(&runtime_), primary_{nullptr, this},
      frame_{{nullptr}, this} {
    // Preserve the constructor's three independent registration points.
    engine::register_owner(bindings_.owners.registry, &primary_,
                           bindings_.owners.memory); //43482185
    bindings_.command_owners.append(command_receiver_, &command_owner_); //43482192
    engine::register_frame_update(bindings_.frame_updates, &frame_); //4348219A
    primary_.vtable = primary_table.data(); //434821A1
    bindings_.command_owners.publish(command_receiver_, client::CommandOwnerId::rebuy); //434821A8
    frame_.vtable = frame_table.data(); //434821B1
    initialize_rebuy_config(bindings_.configuration, bindings_.settings);
    runtime_.enabled = &bindings_.settings.enabled.value;
    runtime_.engine = bindings_.miscellaneous.interfaces.engine;
    runtime_.entity_list = bindings_.miscellaneous.interfaces.entity_list;
    runtime_.buy_anywhere_cvar = bindings_.buy_anywhere_cvar;
    runtime_.local_player = bindings_.miscellaneous.interfaces.local_player;
    runtime_.current_time = bindings_.current_time;
    runtime_.time_4347a500 = bindings_.time_4347a500;
    runtime_.time_4347b118 = bindings_.time_4347b118;
    runtime_.offsets = bindings_.offsets;
    runtime_.bind_spawn(bindings_.events);
    bindings_.miscellaneous.rebuy_fix = &runtime_;
}
RebuyFixOwner::~RebuyFixOwner() {
    //4332B079 disables only the conditional item_purchase model. The native
    //player_spawn triple remains in the global listener vector until teardown.
    runtime_.disable_purchase_subscription(bindings_.events);
    if (bindings_.miscellaneous.rebuy_fix == &runtime_)
        bindings_.miscellaneous.rebuy_fix = nullptr;
    //The Runtime member's vector releases its inventory before owner storage.
}
void RebuyFixOwner::frame_update(std::uint8_t notify) {
    if (notify)
        runtime_.sync_purchase_subscription(bindings_.events);
}
void RebuyFixOwner::destroy() {
    const auto memory = bindings_.owners.memory;
    std::destroy_at(this);
    engine::release_owner_storage(memory, this);
}
RebuyFixOwner* create_rebuy_fix_owner(RebuyFixOwnerBindings bindings) {
    const auto size = std::max(std::size_t(0x54), sizeof(RebuyFixOwner));
    auto* const storage = engine::allocate_owner_storage(bindings.owners.memory, size);
    // The original allocation-failure edge reaches unresolved INT1; source allocation failure terminates this factory with bad_alloc.
    if (!storage)
        throw std::bad_alloc{};
    return ::new (storage) RebuyFixOwner(bindings);
}
} //namespace skeet::misc
