#include "skeet/hud/penetration_reticle_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::hud {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define RETICLE_MEMBER __thiscall
#define RETICLE_FAST __fastcall
#else
#define RETICLE_MEMBER
#define RETICLE_FAST
#endif
using OwnerEntry = void(RETICLE_MEMBER*)(void*);
struct PenetrationReticleInstance;
struct RenderEndpoint : HudFrameCallback {
    PenetrationReticleInstance* owner;
};
void RETICLE_MEMBER destruction(void*);
void RETICLE_MEMBER unchanged(void*) {} //433CCE8E
void RETICLE_MEMBER reset(void*);
void RETICLE_MEMBER refresh(void*);
void RETICLE_FAST draw(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> primary_table{
    destruction, unchanged, unchanged, unchanged, unchanged, reset, unchanged, refresh, unchanged};
const std::array<HudFrameEntry, 1> render_table{draw};

struct PenetrationReticleInstance {
    const OwnerEntry* vtable = nullptr;
    client::NativeCommandPhaseReceiver command_receiver;
    RenderEndpoint render{{nullptr}, this};
    client::PenetrationReticleCommandOwner& command;
    const PenetrationHudBinding& rendering;
    const engine::JobMemory& memory;

    PenetrationReticleInstance(const PenetrationReticleOwnerRegistries& registries,
                               ConfigRegistries& config, esp::ScreenOverlayConfig& settings,
                               client::PenetrationReticleCommandOwner& producer,
                               PenetrationReticleEnvironment& environment,
                               void* const& engine_trace, const PenetrationHudBinding& visual)
        : command(producer), rendering(visual), memory(registries.lifecycle.memory) {
        //434824D1..43482511: all three appends precede table publication and
        // descriptor construction. The command registry invokes the existing
        // concrete4334B1DA producer, sharing this same state/environment.
        registries.lifecycle.append(this);
        registries.commands.append(command_receiver, &command);
        register_hud_frame(registries.hud, &render);
        vtable = primary_table.data();
        registries.commands.publish(command_receiver,
                                    client::CommandOwnerId::penetration_reticle); //434824F4
        render.vtable = render_table.data();
        initialize_boolean_config(config.descriptors, settings.penetration_reticle, 0x1E7432A5u);
        command.state.color = 0;
        //4348251A is the recorded restored instruction A1 04 83 46 43:
        //MOV EAX,[43468304], followed by MOV[EBX+30],EAX. The constructor
        //does not write ready+2C; the reset method owns that separate write.
        environment.point_contents = engine_trace;
    }
};
static_assert(sizeof(PenetrationReticleInstance) <= penetration_reticle_native_owner_size);
void RETICLE_MEMBER destruction(void* storage) {
    destroy_penetration_reticle_owner(storage);
}
void RETICLE_MEMBER reset(void* storage) {
    reset_penetration_reticle_owner(storage);
}
void RETICLE_MEMBER refresh(void* storage) {
    refresh_penetration_reticle_owner(storage);
}
void RETICLE_FAST draw(void* callback, ui::RenderQueue* queue, void* player, std::int32_t* cursor) {
    auto& owner = *static_cast<RenderEndpoint*>(static_cast<HudFrameCallback*>(callback))->owner;
    owner.rendering.draw(*queue, player, *cursor);
}
void* allocate(engine::JobMemory memory) {
    constexpr auto length = penetration_reticle_native_owner_size;
    auto* const raw = memory.allocate(memory.process_heap, 8, length + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address), total = std::uint32_t(length + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &total, 4);
    return reinterpret_cast<void*>(header + 16);
}
#undef RETICLE_MEMBER
#undef RETICLE_FAST
} //namespace
std::size_t penetration_reticle_owner_size() noexcept {
    return penetration_reticle_native_owner_size;
}
void construct_penetration_reticle_owner(
    void* storage, const PenetrationReticleOwnerRegistries& registries, ConfigRegistries& config,
    esp::ScreenOverlayConfig& settings, client::PenetrationReticleCommandOwner& command,
    PenetrationReticleEnvironment& environment, void* const& engine_trace,
    const PenetrationHudBinding& rendering) {
    ::new (storage) PenetrationReticleInstance(registries, config, settings, command, environment,
                                               engine_trace, rendering);
}
void* allocate_penetration_reticle_owner_storage(engine::JobMemory memory) {
    return allocate(memory);
}
void construct_allocated_penetration_reticle_owner(
    void* storage, const PenetrationReticleOwnerRegistries& registries, ConfigRegistries& config,
    esp::ScreenOverlayConfig& settings, client::PenetrationReticleCommandOwner& command,
    PenetrationReticleEnvironment& environment, void* const& engine_trace,
    const PenetrationHudBinding& rendering) {
    settings.penetration_reticle = BooleanConfig{};
    command.state = PenetrationReticleState{};
    construct_penetration_reticle_owner(storage, registries, config, settings, command, environment,
                                        engine_trace, rendering);
}
void destroy_penetration_reticle_owner(void* storage) {
    if (!storage)
        return;
    auto* const owner = static_cast<PenetrationReticleInstance*>(storage);
    const auto memory = owner->memory;
    owner->~PenetrationReticleInstance();
    auto* const bytes = static_cast<std::byte*>(storage);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void reset_penetration_reticle_owner(void* storage) {
    auto& state = static_cast<PenetrationReticleInstance*>(storage)->command.state;
    state.color = 0xFF3200FFu;
    state.ready = false;
}
void refresh_penetration_reticle_owner(void* storage) {
    static_cast<PenetrationReticleInstance*>(storage)->vtable[5](storage);
}
} //namespace skeet::hud
