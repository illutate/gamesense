#include "skeet/hud/screen_overlay_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::hud {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define OVERLAY_MEMBER __thiscall
#define OVERLAY_FAST __fastcall
#else
#define OVERLAY_MEMBER
#define OVERLAY_FAST
#endif
using OwnerEntry = void(OVERLAY_MEMBER*)(void*);
struct ScreenOverlayInstance;
struct RenderEndpoint : HudFrameCallback {
    ScreenOverlayInstance* owner;
};
void OVERLAY_MEMBER destruction(void*);
void OVERLAY_MEMBER bind(void*);
void OVERLAY_MEMBER unchanged(void*) {} //433CCE8E
void OVERLAY_MEMBER reset_forwarder(void*);
void OVERLAY_MEMBER reset(void*);
void OVERLAY_FAST draw(void*, ui::RenderQueue*, void*, std::int32_t*);
const std::array<OwnerEntry, 9> primary_table{destruction, bind,      unchanged,
                                              unchanged,   unchanged, reset_forwarder,
                                              unchanged,   reset,     unchanged};
const std::array<HudFrameEntry, 1> render_table{draw};

struct ScreenOverlayInstance {
    const OwnerEntry* vtable = nullptr;
    client::NativeCommandPhaseReceiver command_receiver;
    RenderEndpoint render{{nullptr}, this};
    client::ScreenOverlayCommandOwner& command;
    const ScreenOverlayHudBinding& rendering;
    ConfigRegistries& configuration;
    const engine::JobMemory& memory;
    const std::int32_t* override_fov_50;
    const std::int32_t* override_zoom_fov_54;

    ScreenOverlayInstance(const ScreenOverlayOwnerRegistries& registries, ConfigRegistries& config,
                          esp::ScreenOverlayConfig& settings,
                          client::ScreenOverlayCommandOwner& producer,
                          const ScreenOverlayHudBinding& visual)
        : command(producer), rendering(visual), configuration(config),
          memory(registries.lifecycle.memory) {
        //4348DBD9..4348DC04. Every registry append precedes table publication
        // and descriptor construction; the existing command dispatcher owns
        // the actual433361BA phase body and its live history-owner cell.
        registries.lifecycle.append(this);
        registries.commands.append(command_receiver, &command);
        register_hud_frame(registries.hud, &render);
        vtable = primary_table.data();
        registries.commands.publish(command_receiver,
                                    client::CommandOwnerId::screen_overlay); //4348DBFC
        render.vtable = render_table.data();
        initialize_boolean_config(config.descriptors, settings.recoil, 0x78A1B150u);
        initialize_boolean_config(config.descriptors, settings.inaccuracy, 0xEDF6C582u);
        initialize_color_config(config.descriptors, settings.inaccuracy_color, 0x2B61D25Au,
                                0x3C1E1E1Eu);
        initialize_boolean_config(config.descriptors, settings.custom_scope, 0xAD37A2ECu);
        initialize_color_config(config.descriptors, settings.custom_scope_color, 0xAD37A2EDu,
                                0xFFFFFFFFu);
        initialize_integer_config(config.descriptors, settings.custom_scope_position, 0xAD37A2EEu,
                                190);
        initialize_integer_config(config.descriptors, settings.custom_scope_offset, 0xAD37A2EFu,
                                15);
        initialize_integer_config(config.descriptors, settings.custom_scope_speed, 0xAD37A2F0u, 12);
        override_fov_50 = nullptr;
        override_zoom_fov_54 = nullptr;
        command.state.byte_58 = nullptr;
        command.state.radius_5c = 0.0f;
        //4348DC58 clears AL-sized storage only. The other pitch bytes and
        // the entire yaw/roll fields retain the incoming canonical state.
        const std::uint8_t zero = 0;
        std::memcpy(&command.state.recoil_pitch_60, &zero, sizeof zero);
        command.state.byte_6c = false;
    }
};
static_assert(sizeof(ScreenOverlayInstance) <= screen_overlay_native_owner_size);
void OVERLAY_MEMBER destruction(void* storage) {
    destroy_screen_overlay_owner(storage);
}
void OVERLAY_MEMBER bind(void* storage) {
    bind_screen_overlay_owner_settings(storage);
}
void OVERLAY_MEMBER reset_forwarder(void* storage) { //433B06E3 ->current primaryslot7
    static_cast<ScreenOverlayInstance*>(storage)->vtable[7](storage);
}
void OVERLAY_MEMBER reset(void* storage) {
    reset_screen_overlay_state(static_cast<ScreenOverlayInstance*>(storage)->command.state);
}
void OVERLAY_FAST draw(void* callback, ui::RenderQueue* queue, void* player, std::int32_t* cursor) {
    auto& owner = *static_cast<RenderEndpoint*>(static_cast<HudFrameCallback*>(callback))->owner;
    owner.rendering.draw(*queue, player, *cursor);
}
void* allocate(engine::JobMemory memory, std::size_t length) {
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
#undef OVERLAY_MEMBER
#undef OVERLAY_FAST
} //namespace
std::size_t screen_overlay_owner_size() noexcept {
    return screen_overlay_native_owner_size;
}
void construct_screen_overlay_owner(void* storage, const ScreenOverlayOwnerRegistries& registries,
                                    ConfigRegistries& config, esp::ScreenOverlayConfig& settings,
                                    client::ScreenOverlayCommandOwner& command,
                                    const ScreenOverlayHudBinding& rendering) {
    ::new (storage) ScreenOverlayInstance(registries, config, settings, command, rendering);
}
void* allocate_screen_overlay_owner_storage(engine::JobMemory memory) {
    return allocate(memory, screen_overlay_native_owner_size);
}
void construct_allocated_screen_overlay_owner(void* storage,
                                              const ScreenOverlayOwnerRegistries& registries,
                                              ConfigRegistries& config,
                                              esp::ScreenOverlayConfig& settings,
                                              client::ScreenOverlayCommandOwner& command,
                                              const ScreenOverlayHudBinding& rendering) {
    settings.recoil = BooleanConfig{};
    settings.inaccuracy = BooleanConfig{};
    settings.inaccuracy_color = ColorConfig{};
    command.state = ScreenOverlayState{};
    construct_screen_overlay_owner(storage, registries, config, settings, command, rendering);
}
void destroy_screen_overlay_owner(void* storage) {
    if (!storage)
        return; //433837D0's native null branch
    auto* const owner = static_cast<ScreenOverlayInstance*>(storage);
    const auto memory = owner->memory;
    owner->~ScreenOverlayInstance();
    auto* const bytes = static_cast<std::byte*>(storage);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void bind_screen_overlay_owner_settings(void* storage) {
    auto& owner = *static_cast<ScreenOverlayInstance*>(storage);
    auto& descriptors = owner.configuration.descriptors;
    owner.override_fov_50 =
        &static_cast<IntegerConfig*>(find_config(descriptors, 0xFC225A4Fu))->value;
    owner.override_zoom_fov_54 =
        &static_cast<IntegerConfig*>(find_config(descriptors, 0xCA80F9EDu))->value;
    owner.command.state.byte_58 =
        &static_cast<BooleanConfig*>(find_config(descriptors, 0x63660060u))->value;
}
} //namespace skeet::hud
