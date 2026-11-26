#include "skeet/anti_aim/owner_lifecycle.hpp"
#include "skeet/animation/state_copy.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <new>

namespace skeet::anti_aim {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define AA_MEMBER __thiscall
#define AA_FAST __fastcall
#else
#define AA_MEMBER
#define AA_FAST
#endif
using PrimaryEntry = void(AA_MEMBER*)(void*);
struct Instance;
struct HudEndpoint : hud::HudFrameCallback {
    Instance* owner;
};
struct Text {
    char* begin = nullptr;
    char* end = nullptr;
    char* limit = nullptr;
};
void AA_MEMBER destroy_entry(void*);
void AA_MEMBER bind_entry(void*);
void AA_MEMBER unchanged(void*) {} //433CCE8E
void AA_MEMBER reset_entry(void*);
void AA_MEMBER refresh_entry(void*);
void AA_FAST hud_entry(void*, ui::RenderQueue*, void*, std::int32_t*);
void AA_FAST frame_entry(void*, std::uint8_t) {} //43471F08 ->433CCE8E
const std::array<PrimaryEntry, 9> primary_table{destroy_entry, bind_entry,    unchanged,
                                                unchanged,     unchanged,     reset_entry,
                                                unchanged,     refresh_entry, unchanged};
const std::array<hud::HudFrameEntry, 1> hud_table{hud_entry};
const std::array<engine::FrameUpdateEntry, 1> frame_table{frame_entry};
void* allocate(engine::JobMemory memory, std::size_t size) {
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
void construct_text(Text& text, engine::JobMemory memory) {
    //433B2213 ->433C1360, including its trailing zero and12-byte capacity.
    text.begin = nullptr;
    text.end = nullptr;
    text.limit = nullptr;
    auto* const bytes = static_cast<char*>(allocate(memory, 12));
    text.begin = bytes;
    text.end = bytes;
    text.limit = bytes + 12;
    std::memmove(bytes, "+spray_menu", 11);
    text.end = bytes + 11;
    bytes[11] = 0;
}
void destroy_text(Text& text, engine::JobMemory memory) {
    const auto capacity =
        std::bit_cast<std::int32_t>(std::uint32_t(reinterpret_cast<std::uintptr_t>(text.limit)) -
                                    std::uint32_t(reinterpret_cast<std::uintptr_t>(text.begin)));
    if (capacity >= 2)
        free(memory, text.begin);
}
struct Instance {
    const PrimaryEntry* vtable = nullptr;
    engine::JobMemory memory;
    ConfigRegistries& config;
    OwnerBindings bindings;
    client::NativeCommandPhaseReceiver command_receiver;
    HudEndpoint hud{{nullptr}, this};
    engine::FrameUpdateEndpoint frame;
    Text command_text;

    Instance(const OwnerRegistries& registries, ConfigRegistries& configurations,
             OwnerBindings context)
        : memory(registries.primary.memory), config(configurations), bindings(context) {
        registries.primary.append(this); //433676A0, complete+0
        registries.commands.append(command_receiver, &bindings.command); //43490448,+8
        hud::register_hud_frame(registries.hud, &hud); //433D20B5,+10
        engine::register_frame_update(registries.frames, &frame); //433FCB12,+18
        registries.commands.publish(command_receiver, client::CommandOwnerId::anti_aim); //4349045F
        hud.vtable = hud_table.data(); //43472654
        vtable = primary_table.data(); //4346F834
        frame.vtable = frame_table.data(); //43471F08
        auto& command = bindings.command;
        initialize_owner_config(config, command.config);
        initialize_command_state_prefix(command.state, command.precalculation, command.parameters);
        construct_text(command_text, memory);
        initialize_command_animation_state(command.state, command.finisher);
        finish_owner_config(command.config);
        initialize_command_pattern(command.parameters);
    }
};
void AA_MEMBER destroy_entry(void* storage) {
    auto* const owner = static_cast<Instance*>(storage);
    destroy_owner(storage, owner->bindings.active);
}
void AA_MEMBER bind_entry(void* storage) {
    bind_owner_settings(storage);
}
void AA_MEMBER reset_entry(void* storage) {
    reset_state(static_cast<Instance*>(storage)->bindings.command.state);
}
void AA_MEMBER refresh_entry(void* storage) {
    refresh_owner_command(storage);
}
void AA_FAST hud_entry(void* endpoint, ui::RenderQueue* queue, void* player, std::int32_t* cursor) {
    auto& owner = *static_cast<HudEndpoint*>(static_cast<hud::HudFrameCallback*>(endpoint))->owner;
    draw_owner_hud(owner.bindings.command, owner.bindings.hud, *queue, player, *cursor);
}
}
std::size_t owner_size() noexcept {
    return sizeof(Instance);
}
void construct_owner(void* storage, const OwnerRegistries& registries, ConfigRegistries& config,
                     OwnerBindings bindings) {
    ::new (storage) Instance(registries, config, bindings);
}
void* create_owner(const OwnerRegistries& registries, ConfigRegistries& config,
                   OwnerBindings bindings) {
    void* const storage = allocate(registries.primary.memory, owner_size());
    auto& command = bindings.command;
    command.config = Config{};
    command.state = AngleStageState{};
    command.state.cached_yaw_18c = 0.0f;
    command.parameters = {0, 0, 0, 0, 0, 0, 0, 0, 0, {}};
    command.precalculation.feet_yaw_194 = 0.0f;
    command.precalculation.micro_move_19c = 0.0f;
    command.precalculation.previous_duck_1a4 = 0.0f;
    std::fill(command.finisher.animation_1d8.begin(), command.finisher.animation_1d8.end(),
              std::byte{0});
    command.finisher.absolute_angles_1b8 = {};
    std::fill(command.finisher.matrices_520.begin(), command.finisher.matrices_520.end(),
              Matrix3x4{});
    construct_owner(storage, registries, config, bindings);
    return storage;
}
void destroy_owner(void* storage, CommandOwner*& active) {
    active = nullptr;
    if (!storage)
        return;
    auto* const owner = static_cast<Instance*>(storage);
    const auto memory = owner->memory;
    animation::release_animation_state_vector(owner->bindings.command.finisher.animation_1d8.data(),
                                              owner->bindings.allocator);
    destroy_text(owner->command_text, memory);
    owner->~Instance();
    free(memory, storage);
}
void bind_owner_settings(void* storage) {
    auto& owner = *static_cast<Instance*>(storage);
    owner.bindings.command.state.build_bones_12c =
        &static_cast<BooleanConfig*>(find_config(owner.config.descriptors, 0x96DB2E1Cu))->value;
}
void refresh_owner_command(void* storage) {
    auto& owner = *static_cast<Instance*>(storage);
    owner.vtable[5](storage);
    const auto command =
        engine::invoke<std::int32_t>(owner.bindings.command.engine_client, 170,
                                     static_cast<const char*>(owner.command_text.begin),
                                     std::int32_t(-1), std::int32_t(0), std::int32_t(-1));
    owner.bindings.command.state.value_1c4 = command == 0 ? -1 : command;
}
void draw_owner_hud(CommandOwner& command, const OwnerHudBindings& bindings, ui::RenderQueue& queue,
                    void* player, std::int32_t& cursor) {
    if (!command.global.enabled.value)
        return;
    esp::RuntimePlayer local;
    local.engine_entity = player;
    std::memcpy(&local.life_state,
                static_cast<const std::byte*>(player) + bindings.life_state_9d920e35,
                sizeof local.life_state);
    hud::draw_freestanding_indicator(queue, bindings.scripts, command.global, command.config,
                                     command.state, bindings.indicators, local,
                                     command.globals.input, cursor, bindings.layout);
}
#undef AA_MEMBER
#undef AA_FAST
} //namespace skeet::anti_aim
