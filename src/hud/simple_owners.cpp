#include "skeet/hud/simple_owners.hpp"
#include "skeet/misc/reveal_ranks.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::hud {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SIMPLE_MEMBER __thiscall
#define SIMPLE_HUD __fastcall
#else
#define SIMPLE_MEMBER
#define SIMPLE_HUD
#endif
using OwnerEntry = void(SIMPLE_MEMBER*)(void*);
const SimpleHudOwnersContext* process_context = nullptr;
void SIMPLE_MEMBER destroy(void* storage) {
    destroy_simple_hud_owner(storage);
}
void SIMPLE_MEMBER unchanged(void*) {} //433CCE8E is exactly RET
void SIMPLE_MEMBER reset_ping(void* storage) {
    reset_ping_hud_history(storage);
}
void SIMPLE_MEMBER bind_crosshair(void* storage) {
    bind_crosshair_hud_variables(storage);
}
void SIMPLE_HUD draw_ping(void*, ui::RenderQueue*, void*, std::int32_t*);
void SIMPLE_HUD draw_crosshair_owner(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> ping_primary{
    destroy,   unchanged, unchanged,  unchanged, unchanged,
    unchanged, unchanged, reset_ping, unchanged}; //43472364
const std::array<HudFrameEntry, 1> ping_render{draw_ping}; //434727D0
const std::array<OwnerEntry, 9> crosshair_primary{destroy,   bind_crosshair, unchanged,
                                                  unchanged, unchanged,      unchanged,
                                                  unchanged, unchanged,      unchanged}; //43470660
const std::array<HudFrameEntry, 1> crosshair_render{draw_crosshair_owner}; //4346F878

// No member initializer may touch the HUD cell before its native registrar.
// These are the actual first-slot ABI cells, not portable binding wrappers.
struct NativeHudEndpoint {
    const HudFrameEntry* vtable;
};
struct PingOwner {
    const OwnerEntry* vtable;
    std::uint32_t retained_4;
    NativeHudEndpoint render;
};
struct CrosshairOwner {
    const OwnerEntry* vtable;
    std::uint32_t retained_4;
    NativeHudEndpoint render;
    const void* style_c;
    const void* weapon_mode_10;
};
static_assert(sizeof(void*) == 4, "The native simple HUD owners require Windows x86");
static_assert(sizeof(PingOwner) == ping_hud_native_owner_size && offsetof(PingOwner, render) == 8);
static_assert(sizeof(CrosshairOwner) == crosshair_hud_native_owner_size &&
              offsetof(CrosshairOwner, render) == 8 && offsetof(CrosshairOwner, style_c) == 0xc &&
              offsetof(CrosshairOwner, weapon_mode_10) == 0x10);
static_assert(sizeof(misc::PingSpikeNetworkHistory) == 0x1830);

template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::uint32_t offset(std::uint32_t pointer, std::uint32_t key, std::uint32_t mask) {
    const auto& globals = process_context->offsets.globals;
    return *globals.pointers.at(pointer) ^ globals.keys.at(key) ^ mask;
}
void* allocate(std::size_t size) { //complete43387448, exact native size
    const auto memory = process_context->lifecycle.memory;
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address), total = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &total, 4);
    return reinterpret_cast<void*>(header + 16);
}
void SIMPLE_HUD draw_ping(void*, ui::RenderQueue* queue, void* local, std::int32_t* cursor) {
    const auto& context = *process_context;
    if (!(std::uint32_t(context.feature_indicators.value) & 4u))
        return;
    if (field<std::uint8_t>(local, offset(0x43468e78, 0x434684e4, 0x9d920e35)))
        return;
    if (!context.ping_channel.feature.active())
        return;
    draw_ping_latency_indicator(*queue, context.scripts, context.ping_channel.feature, *cursor,
                                context.indicator_layout);
}
void SIMPLE_HUD draw_crosshair_owner(void* callback, ui::RenderQueue* queue, void* local,
                                     std::int32_t*) {
    const auto& context = *process_context;
    const auto* owner = reinterpret_cast<const CrosshairOwner*>(
        static_cast<const std::byte*>(callback) - offsetof(CrosshairOwner, render));
    const NativeCrosshairBindings bindings{context.entity_list, owner->style_c,
                                           owner->weapon_mode_10, context.offsets};
    draw_live_crosshair(*queue, context.overlays, bindings, local, context.suspended);
}
#undef SIMPLE_MEMBER
#undef SIMPLE_HUD
} //namespace

void bind_simple_hud_owners_context(const SimpleHudOwnersContext& context) {
    if (process_context && process_context != &context)
        throw std::logic_error("Simple HUD callbacks require one stable process context");
    process_context = &context;
}
void initialize_ping_hud_owner() { //whole4347DD87, including its failed-allocation RET
    if (auto* storage = allocate(ping_hud_native_owner_size))
        construct_ping_hud_owner(storage);
}
void initialize_crosshair_hud_owner() { //whole43486BF7, including its failed-allocation RET
    if (auto* storage = allocate(crosshair_hud_native_owner_size))
        construct_crosshair_hud_owner(storage);
}
void construct_ping_hud_owner(void* storage) { //4347DD96..4347DDBA
    auto* owner = ::new (storage) PingOwner;
    owner->vtable = nullptr;
    process_context->lifecycle.append(owner);
    owner->render.vtable = nullptr;
    register_hud_frame(process_context->hud, reinterpret_cast<HudFrameCallback*>(&owner->render));
    owner->render.vtable = ping_render.data();
    owner->vtable = ping_primary.data();
}
void construct_crosshair_hud_owner(void* storage) { //43486C06..43486C2A
    auto* owner = ::new (storage) CrosshairOwner;
    owner->vtable = nullptr;
    process_context->lifecycle.append(owner);
    register_hud_frame(process_context->hud, reinterpret_cast<HudFrameCallback*>(&owner->render));
    owner->style_c = nullptr;
    owner->vtable = crosshair_primary.data();
    owner->render.vtable = crosshair_render.data();
}
void destroy_simple_hud_owner(void* storage) {
    if (!storage)
        return;
    const auto memory = process_context->lifecycle.memory;
    const auto displacement = field<std::uint32_t>(storage, -8);
    memory.free(memory.process_heap, 0, static_cast<std::byte*>(storage) - 16 - displacement);
}
void reset_ping_hud_history(void*) {
    std::memset(&process_context->ping_channel.bindings.history, 0, 0x1830);
}
void bind_crosshair_hud_variables(void* storage) {
    auto& owner = *static_cast<CrosshairOwner*>(storage);
    owner.style_c = misc::find_reveal_ranks_command(process_context->cvar, 0xe95e1c63u);
    owner.weapon_mode_10 = misc::find_reveal_ranks_command(process_context->cvar, 0xb468ad35u);
}
} //namespace skeet::hud
