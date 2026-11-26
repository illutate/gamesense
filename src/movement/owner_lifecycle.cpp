#include "skeet/movement/owner_lifecycle.hpp"
#include "skeet/movement/initialization.hpp"
#include "skeet/movement/command_bindings.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <new>

namespace skeet {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define MOVEMENT_MEMBER __thiscall
#define MOVEMENT_FAST __fastcall
#else
#define MOVEMENT_MEMBER
#define MOVEMENT_FAST
#endif
using OwnerEntry = void(MOVEMENT_MEMBER*)(void*);
struct MovementInstance;
struct HudEndpoint : hud::HudFrameCallback {
    MovementInstance* owner;
};
struct OwnedText {
    char* begin = nullptr;
    char* end = nullptr;
    char* limit = nullptr;
};
void* allocate(engine::JobMemory memory, std::size_t size) {
    if (!size)
        return nullptr;
    auto* const raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    const auto total = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &total, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free(engine::JobMemory memory, void* object) {
    if (!object)
        return;
    auto* const bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void construct_text(OwnedText& text, const char* value, std::size_t length,
                    engine::JobMemory memory) {
    // Whole433B2213 ->433C1360 for the two nonempty native literals. The
    // allocation includes the trailing zero, and end excludes that zero.
    text.begin = nullptr;
    text.end = nullptr;
    text.limit = nullptr;
    auto* const bytes = static_cast<char*>(allocate(memory, length + 1));
    text.begin = bytes;
    text.end = bytes;
    text.limit = bytes + length + 1;
    std::memmove(bytes, value, length);
    text.end = bytes + length;
    bytes[length] = 0;
}
void destroy_text(OwnedText& text, engine::JobMemory memory) {
    const auto capacity =
        std::bit_cast<std::int32_t>(std::uint32_t(reinterpret_cast<std::uintptr_t>(text.limit)) -
                                    std::uint32_t(reinterpret_cast<std::uintptr_t>(text.begin)));
    if (capacity >= 2 && text.begin)
        free(memory, text.begin);
    //4336B1E9 does not clear the three string words after freeing.
}
void MOVEMENT_MEMBER destruction_entry(void*);
void MOVEMENT_MEMBER binding_entry(void*);
void MOVEMENT_MEMBER unchanged(void*) {} //actual433CCE8E
void MOVEMENT_MEMBER reset_entry(void*);
void MOVEMENT_MEMBER refresh_entry(void*);
void MOVEMENT_FAST hud_entry(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> owner_table{destruction_entry, binding_entry, unchanged,
                                            unchanged,         unchanged,     reset_entry,
                                            unchanged,         refresh_entry, unchanged};
const std::array<hud::HudFrameEntry, 1> hud_table{hud_entry};

struct MovementInstance {
    const OwnerEntry* vtable = nullptr;
    client::NativeCommandPhaseReceiver command_receiver;
    HudEndpoint hud{{nullptr}, this};
    MovementOwnerBindings bindings;
    ConfigRegistries& config;
    engine::JobMemory memory;
    OwnedText command_text, label_text; //native full+54C,full+55C

    MovementInstance(const MovementOwnerRegistries& registries, ConfigRegistries& configurations,
                     MovementOwnerBindings context)
        : bindings(context), config(configurations), memory(registries.lifecycle.memory) {
        registries.lifecycle.append(this);
        registries.commands.append(command_receiver, &bindings.command);
        hud::register_hud_frame(registries.hud, &hud);
        vtable = owner_table.data();
        registries.commands.publish(command_receiver, client::CommandOwnerId::movement); //4348FAB6
        hud.vtable = hud_table.data();
        auto& state = bindings.command.state;
        initialize_movement_state(state, config, bindings.air_accelerate, bindings.jump_speed_cap);
        construct_text(command_text, "+duck", 5, memory);
        construct_text(label_text, "DUCK", 4, memory);
        finish_movement_state_initialization(state);
    }
};
void MOVEMENT_MEMBER destruction_entry(void* storage) {
    auto* const owner = static_cast<MovementInstance*>(storage);
    destroy_movement_owner(storage, owner->bindings.active);
}
void MOVEMENT_MEMBER binding_entry(void* storage) {
    auto& owner = *static_cast<MovementInstance*>(storage);
    bind_movement_settings(owner.bindings.command.state, owner.config);
}
void MOVEMENT_MEMBER reset_entry(void* storage) {
    reset_movement_state(static_cast<MovementInstance*>(storage)->bindings.command.state);
}
void MOVEMENT_MEMBER refresh_entry(void* storage) {
    refresh_movement_owner_key(storage);
}
void MOVEMENT_FAST hud_entry(void* callback, ui::RenderQueue* queue, void* player,
                             std::int32_t* cursor) {
    auto& owner = *static_cast<HudEndpoint*>(static_cast<hud::HudFrameCallback*>(callback))->owner;
    draw_movement_owner_hud(owner.bindings.command, owner.bindings.hud, *queue, player, *cursor);
}
#undef MOVEMENT_MEMBER
#undef MOVEMENT_FAST
} // namespace

std::size_t movement_owner_size() noexcept {
    return sizeof(MovementInstance);
}
void construct_movement_owner(void* storage, const MovementOwnerRegistries& registries,
                              ConfigRegistries& config, MovementOwnerBindings bindings) {
    ::new (storage) MovementInstance(registries, config, bindings);
    bindings.active = &bindings.command.state;
}
void* create_movement_owner(const MovementOwnerRegistries& registries, ConfigRegistries& config,
                            MovementOwnerBindings bindings) {
    void* const storage = allocate(registries.lifecycle.memory, movement_owner_size());
    if (storage)
        construct_movement_owner(storage, registries, config, bindings);
    else
        bindings.active = nullptr;
    return storage;
}
void destroy_movement_owner(void* storage, MovementCommandOwner*& active) {
    active = nullptr;
    if (!storage)
        return;
    auto* const owner = static_cast<MovementInstance*>(storage);
    const auto memory = owner->memory;
    destroy_text(owner->label_text, memory);
    destroy_text(owner->command_text, memory);
    owner->~MovementInstance();
    free(memory, storage);
}
void refresh_movement_owner_key(void* storage) {
    auto& owner = *static_cast<MovementInstance*>(storage);
    owner.vtable[5](storage);
    const auto command = engine::invoke<std::int32_t>(
        owner.bindings.engine_client, 170, static_cast<const char*>(owner.command_text.begin),
        std::int32_t(-1), std::int32_t(0), std::int32_t(-1));
    if (command != 0 && command != -1)
        owner.bindings.command.state.duck_peek.last_key =
            engine::invoke<std::int32_t>(owner.bindings.input_system, 46, command);
}
void draw_movement_owner_hud(MovementCommandPhaseOwner& command,
                             const MovementOwnerHudBindings& bindings, ui::RenderQueue& queue,
                             void* player, std::int32_t& cursor) {
    esp::RuntimePlayer local;
    local.engine_entity = player;
    std::memcpy(&local.life_state,
                static_cast<const std::byte*>(player) + bindings.life_state_9d920e35,
                sizeof local.life_state);
    auto& state = command.state;
    auto& duck = state.duck_peek;
    hud::MovementIndicatorState render_state{state.quick_peek, duck.phase, duck.target,
                                             duck.last_key, duck.crouch_active};
    hud::draw_movement_indicators(
        queue, bindings.scripts, bindings.feature_indicators, bindings.infinite_duck, bindings.rage,
        bindings.command_flags, state.quick_peek_config, render_state, local,
        command.bindings.command.input, cursor, bindings.layout, bindings.projection);
}
} // namespace skeet
