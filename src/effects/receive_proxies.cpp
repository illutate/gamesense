#include "skeet/effects/receive_proxies.hpp"
#include "skeet/effects/bullet_impact_command_owner.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/esp/player_runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::effects {
namespace {
VisualReceiveRuntime* active = nullptr;
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
void smoke(const engine::RecvProxyData* packet, void* entity, void* output) {
    active->receive_smoke(packet, entity, output);
}
void flash(const engine::RecvProxyData* packet, void* entity, void* output) {
    active->receive_flash(packet, entity, output);
}
void observer(const engine::RecvProxyData* packet, void* entity, void* output) {
    active->receive_observer(packet, entity, output);
}
void fov(const engine::RecvProxyData* packet, void* entity, void* output) {
    active->receive_fov(packet, entity, output);
}
void sun(const engine::RecvProxyData* packet, void* entity, void* output) {
    active->receive_sun(packet, entity, output);
}
constexpr std::array<VisualReceiveDescriptor, 5> descriptors{{
    {0xA98u, 0x71D79C49u, 0xF7DBD1ADu, 0x1362ED63u, 0x4337EDF0u, 0x434769ECu, smoke},
    {0xAACu, 0x5AB40334u, 0xE0281F50u, 0x7763C80Au, 0x4338A0C3u, 0x4347693Cu, flash},
    {0xAC0u, 0xE8D6D5BFu, 0xDC554CD3u, 0x487F26C4u, 0x4340C7B4u, 0x43476A2Cu, observer},
    {0xAD4u, 0xE8D6D5BFu, 0xDC554CD3u, 0xAE5C0B76u, 0x4339EBD4u, 0x4347692Cu, fov},
    {0xAE8u, 0xB66F52E5u, 0x44C7B439u, 0xF9D86BC1u, 0x433E978Bu, 0x43476ACCu, sun},
}};
// Only real engine virtuals are adapted for the complete43359AFA source body.
struct ViewSelection {
    engine::NativeDataImage& data;
    esp::RuntimePlayer local{}, observed{};
    esp::PlayerRuntimeEngine engine{};
    explicit ViewSelection(engine::NativeDataImage& source) : data(source) {
        engine.context = this;
        engine.by_handle = [](void* context, std::int32_t handle) -> esp::RuntimePlayer* {
            auto& self = *static_cast<ViewSelection*>(context);
            auto* entity =
                skeet::engine::invoke<void*>(self.data.native_cell<void*>(0x4346A890u), 4, handle);
            if (!entity)
                return nullptr;
            self.observed.engine_entity = entity;
            return &self.observed;
        };
        engine.is_player = [](void*, const esp::RuntimePlayer& player) {
            return skeet::engine::invoke<bool>(player.engine_entity, 158);
        };
    }
    void* selected() {
        const auto* result = esp::effective_view_player(engine);
        return result ? result->engine_entity : nullptr;
    }
};
}
std::span<const VisualReceiveDescriptor, 5> visual_receive_descriptors() noexcept {
    return descriptors;
}
VisualReceivePatchFailure::VisualReceivePatchFailure(std::size_t at,
                                                     const VisualReceiveDescriptor& row,
                                                     VisualReceivePatchFailureReason why)
    : std::runtime_error("Required visual receive patch cannot be initialized"), index(at),
      native_entry(row.native_entry), patch_address(row.patch_address), reason(why) {}
VisualReceiveRuntime::VisualReceiveRuntime(engine::NativeDataImage& data,
                                           const esp::EffectsSettings& settings,
                                           const engine::NativeOffsetStorage& offsets,
                                           client::PlayerStateCache& players)
    : data_(data), settings_(settings), offsets_(offsets), players_(players) {}
std::uint32_t VisualReceiveRuntime::offset(std::uint32_t pointer, std::uint32_t key,
                                           std::uint32_t salt) const {
    return *offsets_.globals.pointers.at(pointer) ^ offsets_.globals.keys.at(key) ^ salt;
}
void VisualReceiveRuntime::original(std::uint32_t cell, const engine::RecvProxyData* packet,
                                    void* entity, void* output) const {
    data_.native_cell<engine::RecvVarProxy>(cell)(packet, entity, output);
}
void VisualReceiveRuntime::receive_smoke(const engine::RecvProxyData* packet, void* entity,
                                         void* output) {
    if (!data_.native_cell<std::uint8_t>(0x43468E58u) &&
        data_.native_cell<WorldRuntime*>(0x4347585Cu)->settings.remove_smoke.value)
        write(entity, offset(0x43468970u, 0x4346A3A8u, 0x666EC5F2u), std::uint8_t(1));
    original(0x434769F4u, packet, entity, output);
}
void VisualReceiveRuntime::receive_flash(const engine::RecvProxyData* packet, void* entity,
                                         void* output) {
    if (settings_.remove_flash.value) {
        ViewSelection view(data_);
        if (auto* local = data_.native_cell<void*>(0x43475808u)) {
            view.local.engine_entity = local;
            view.engine.local_player = &view.local;
            // Preserve the original helper's short-circuit field reads.
            view.local.life_state =
                read<std::uint8_t>(local, offset(0x43468E78u, 0x434684E4u, 0x9D920E35u));
            if (view.local.life_state) {
                view.local.move_type =
                    read<std::uint8_t>(local, offset(0x43468BD4u, 0x4346A678u, 0x7501CB91u));
                if (view.local.move_type == 10) {
                    view.local.observer_mode =
                        read<std::int32_t>(local, offset(0x4346A6CCu, 0x43468EF8u, 0x2F4E97C4u));
                    if (view.local.observer_mode == 4 || view.local.observer_mode == 5)
                        view.local.observer_handle = read<std::int32_t>(
                            local, offset(0x43468F84u, 0x43467E64u, 0x3B704B41u));
                }
            }
        }
        if (entity == view.selected())
            return;
    }
    original(0x43476944u, packet, entity, output);
}
void VisualReceiveRuntime::capture(const engine::RecvProxyData* packet, void* entity,
                                   bool observer_mode) {
    const auto index = engine::invoke<std::uint32_t>(static_cast<std::byte*>(entity) + 8, 10);
    if (index - 1u <= 63u) {
        auto* state = players_.get(index);
        if (!state)
            throw std::bad_alloc();
        const auto value = read<std::int32_t>(packet, 8);
        if (observer_mode)
            state->native.captured_observer_mode = value;
        else
            state->native.captured_camera_fov = value;
    }
}
void VisualReceiveRuntime::receive_observer(const engine::RecvProxyData* packet, void* entity,
                                            void* output) {
    capture(packet, entity, true);
    if (settings_.third_person_dead.value &&
        read<std::uint8_t>(entity, offset(0x43468BF8u, 0x43467B8Cu, 0xD070684Cu)) &&
        read<std::int32_t>(packet, 8) == 4) {
        write(const_cast<engine::RecvProxyData*>(packet), 8, std::int32_t(5));
        if (engine::invoke<void*>(data_.native_cell<void*>(0x4346A894u), 32, std::int32_t(-1)))
            engine::invoke<void>(data_.native_cell<void*>(0x4346A894u), 36);
    }
    original(0x43476A34u, packet, entity, output);
}
void VisualReceiveRuntime::receive_fov(const engine::RecvProxyData* packet, void* entity,
                                       void* output) {
    capture(packet, entity, false);
    original(0x43476934u, packet, entity, output);
}
void VisualReceiveRuntime::receive_sun(const engine::RecvProxyData* packet, void* entity,
                                       void* output) {
    if (data_.native_cell<EffectsCommandOwner*>(0x43475A1Cu)->world.settings.brightness.value == 2)
        write(const_cast<engine::RecvProxyData*>(packet), 8, std::int32_t(0));
    original(0x43476AD4u, packet, entity, output);
}
void VisualReceiveRuntime::bind_receive_patches() {
    for (const auto& row : descriptors) {
        const auto& node = data_.native_cell<client::EntityUpdatePatch>(row.patch_address);
        if (!node.target || !node.after)
            throw std::invalid_argument(
                "Visual receive patches require genuine captured property targets and originals");
    }
    active = this;
    for (const auto& row : descriptors)
        data_.native_cell<client::EntityUpdatePatch>(row.patch_address).before =
            std::bit_cast<std::int32_t>(
                std::uint32_t(reinterpret_cast<std::uintptr_t>(row.replacement)));
}
void VisualReceiveRuntime::initialize_receive_patches() {
    auto* client = data_.native_cell<void*>(0x434683E0u);
    if (!client)
        throw std::invalid_argument("Visual receive patches require canonical VClient018");
    struct Capture {
        client::EntityUpdatePatch* node;
        engine::RecvProp* property;
        engine::RecvVarProxy original;
    };
    std::array<Capture, 5> captures;
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto& row = descriptors[index];
        auto& node = data_.native_cell<client::EntityUpdatePatch>(row.patch_address);
        if (node.target || node.after || node.before)
            throw VisualReceivePatchFailure(index, row,
                                            VisualReceivePatchFailureReason::already_initialized);
        auto* property = engine::find_receive_property(client, row.class_hash, row.table_hash,
                                                       row.property_hash);
        if (!property)
            throw VisualReceivePatchFailure(index, row,
                                            VisualReceivePatchFailureReason::missing_property);
        if (!property->proxy)
            throw VisualReceivePatchFailure(index, row,
                                            VisualReceivePatchFailureReason::missing_original);
        captures[index] = {&node, property, property->proxy};
    }
    for (const auto& capture : captures) {
        capture.node->target = reinterpret_cast<volatile std::int32_t*>(&capture.property->proxy);
        capture.node->after = std::bit_cast<std::int32_t>(
            std::uint32_t(reinterpret_cast<std::uintptr_t>(capture.original)));
    }
    bind_receive_patches();
}
} //namespace skeet::effects
