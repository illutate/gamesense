#include "owner_detail.hpp"
#include "skeet/rage/initialization.hpp"
#include "skeet/rage/post_firing.hpp"
#include "skeet/client/command_phase_registry.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace skeet::rage {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define RAGE_MEMBER __thiscall
#define RAGE_HUD __fastcall
#else
#define RAGE_MEMBER
#define RAGE_HUD
#endif
using LifecycleEntry = void(RAGE_MEMBER*)(void*);
void RAGE_MEMBER destroy_entry(void*);
void RAGE_MEMBER reset_entry(void*);
void RAGE_MEMBER forward_reset_entry(void*);
void RAGE_MEMBER unchanged_entry(void*) {} //433CCE8E
const std::array<LifecycleEntry, 9> lifecycle_table{
    destroy_entry, unchanged_entry, unchanged_entry,     unchanged_entry, unchanged_entry,
    reset_entry,   unchanged_entry, forward_reset_entry, unchanged_entry};
void RAGE_HUD draw_entry(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<hud::HudFrameEntry, 1> hud_table{draw_entry};
struct HudEndpoint : hud::HudFrameCallback {
    LifecycleOwner* owner;
};

void* allocate(engine::JobMemory memory, std::size_t size) { //43387448
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    const auto allocation_size = static_cast<std::uint32_t>(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free(engine::JobMemory memory, void* object) {
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void require_canonical_bindings(const OwnerBindings& b) {
    const auto& c = b.commands;
    if (&b.hud.config != &b.aimbot || &b.hud.visualization != &b.visualization)
        throw std::invalid_argument(
            "RAGE constructor bindings must share their canonical settings and state");
    c.require_factory(b.aimbot, b.hud.global, b.visualization, b.hud.highlight);
    if (c.acquisition().evaluations.data())
        throw std::invalid_argument("RAGE constructor requires its unallocated evaluation owner");
}
}

class LifecycleOwner {
public:
    const LifecycleEntry* vtable = nullptr;
    OwnerBindings bindings;
    LifecycleOwner** active;
    client::NativeCommandPhaseReceiver command_receiver;
    HudEndpoint hud_endpoint;
    client::NativeEntityObserverReceiver entity_receiver;
    std::int32_t field_174; //the complete owner's retained constructor/reset marker

    LifecycleOwner(OwnerBindings b, LifecycleOwner*& current) : bindings(b), active(&current) {
        bindings.lifecycle.append(this); //433676A0, complete owner+0
        bindings.command_owners.append(command_receiver, &bindings.commands); //434803CE, +8
        hud_endpoint.owner = this;
        hud::register_hud_frame(bindings.hud_owners, &hud_endpoint); //433D20B5, +10
        bindings.entity_owners.append_rage(entity_receiver, bindings.entities); //434803DE, +18
        vtable = lifecycle_table.data(); //434803ED
        bindings.command_owners.publish(command_receiver, client::CommandOwnerId::rage); //434803F4
        hud_endpoint.vtable = hud_table.data(); //434803FB
        bindings.entity_owners.publish_rage(entity_receiver); //43480402
        owner_detail::initialize_settings(bindings.configuration, bindings.aimbot,
                                          bindings.visualization);
        initialize_rage_state(reset_bindings(), bindings.lifecycle.memory);
    }
    RageResetBindings reset_bindings() noexcept {
        auto& command = bindings.commands;
        return {command.state(), command.acquisition(),  command.shot_index(),
                field_174,       command.action_state(), command.pending_angles()};
    }
};

namespace {
void destroy(LifecycleOwner* owner, LifecycleOwner*& active) { //433F19F5/43387719
    active = nullptr;
    if (!owner)
        return;
    *owner->active = nullptr;
    const auto memory = owner->bindings.lifecycle.memory;
    owner->bindings.commands.acquisition().evaluations.release();
    std::destroy_at(owner);
    free(memory, owner);
}
void RAGE_MEMBER destroy_entry(void* object) {
    auto* owner = static_cast<LifecycleOwner*>(object);
    destroy(owner, *owner->active);
}
void RAGE_MEMBER reset_entry(void* object) {
    reset_rage_state(static_cast<LifecycleOwner*>(object)->reset_bindings());
}
void RAGE_MEMBER forward_reset_entry(void* object) { //433CF2F5
    engine::invoke<void>(object, 5);
}
void RAGE_HUD draw_entry(void* object, ui::RenderQueue* queue, void* player, std::int32_t* cursor) {
    static_cast<HudEndpoint*>(object)->owner->bindings.hud.draw(*queue, player, *cursor);
}
#undef RAGE_MEMBER
#undef RAGE_HUD
}

LifecycleOwner* construct_rage_owner(OwnerBindings bindings, LifecycleOwner*& active) { //4348DA25
    require_canonical_bindings(bindings);
    auto* memory = allocate(bindings.lifecycle.memory, sizeof(LifecycleOwner));
    if (!memory) {
        active = nullptr;
        return nullptr;
    }
    auto* owner = ::new (memory) LifecycleOwner(bindings, active);
    active = owner;
    return owner;
}
void destroy_rage_owner(LifecycleOwner*& active) {
    destroy(active, active);
}
void* rage_lifecycle_object(LifecycleOwner& owner) noexcept {
    return &owner;
}
}
