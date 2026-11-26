#include "skeet/client/skin_runtime_factories.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/network/data_update_observers.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
using Bindings = SkinRuntimeFactoryBindings;

void require_bindings(const Bindings& b) {
    const auto& image = b.entity_updates.image();
    if (!b.lifecycle.memory.allocate || !b.lifecycle.memory.free ||
        !b.lifecycle.memory.process_heap)
        throw std::invalid_argument("Skin factories require the canonical process heap");
    if (&b.lifecycle.registry != &image.native_cell<engine::OwnerRegistry>(0x43475B78u) ||
        &b.commands.image() != &image ||
        &b.commands.native_storage() != &image.native_cell<engine::OwnerRegistry>(0x434757A0u) ||
        &b.entity_updates.native_storage() !=
            &image.native_cell<engine::OwnerRegistry>(0x43475BA8u) ||
        &b.agent.observers.image() != &image ||
        &b.agent.observers.native_storage() !=
            &image.native_cell<engine::OwnerRegistry>(0x43475760u))
        throw std::invalid_argument(
            "Skin factories require their canonical owner and observer registries");
    if (&b.configuration.descriptors != &image.native_cell<ConfigLink>(0x434798BCu) ||
        &b.configuration.hotkeys != &image.native_cell<HotkeyLink>(0x434798C4u) ||
        &b.inventory.weapons != &b.settings.weapons || &b.cosmetics.settings != &b.settings ||
        &b.cosmetics.cosmetics.settings() != &b.settings || &b.agent.settings != &b.settings)
        throw std::invalid_argument(
            "Skin factories must share the canonical configuration and Settings");
    const auto& receive = b.agent.receiver.receive_bindings();
    const auto& capture = b.agent.receiver.capture_globals();
    if (&b.inventory.engine_client != &image.native_cell<void*>(0x4346A514u) ||
        &b.cosmetics.engine_client != &b.inventory.engine_client ||
        &receive.engine_client != &b.inventory.engine_client ||
        &b.inventory.entity_list != &image.native_cell<void*>(0x4346A890u) ||
        &b.agent.entity_list != &b.inventory.entity_list ||
        &b.cosmetics.local_player != &image.native_cell<void*>(0x43475808u) ||
        &b.agent.offsets != &b.inventory.offsets ||
        &capture.original_handle != &image.native_cell<std::uint32_t>(0x4346D648u) ||
        &capture.original_model != &image.native_cell<std::int32_t>(0x4346D650u))
        throw std::invalid_argument(
            "Skin factories must share their live engine and agent capture cells");
}

void require_fresh(std::initializer_list<const ConfigLink*> descriptors) {
    for (const auto* descriptor : descriptors)
        if (descriptor->next || descriptor->previous)
            throw std::invalid_argument("Skin factory descriptors must be fresh and unlinked");
}
template <class T> void zero_descriptor(ConfigValue<T>& descriptor) {
    descriptor.next = descriptor.previous = nullptr;
    descriptor.hash = 0;
    descriptor.serialized_size = 0;
    descriptor.type = static_cast<ConfigType>(0);
    descriptor.reserved = 0;
    descriptor.value = {};
}
void* allocate_owner(engine::JobMemory memory, std::size_t native_size, std::size_t source_size) {
    const auto size = std::max(native_size, source_size);
    auto* storage = engine::allocate_owner_storage(memory, size); //43387448
    // Each original null edge enters an unresolved INT1 continuation. The
    // source failure policy is bad_alloc; it does not emulate that transfer.
    if (!storage)
        throw std::bad_alloc{};
    return storage;
}
void inventory_entry(void* context) {
    (void)create_skin_inventory_owner(*static_cast<Bindings*>(context));
}
void glove_entry(void* context) {
    (void)create_skin_glove_owner(*static_cast<Bindings*>(context));
}
void mask_entry(void* context) {
    (void)create_skin_mask_owner(*static_cast<Bindings*>(context));
}
void agent_entry(void* context) {
    (void)create_skin_agent_owner(*static_cast<Bindings*>(context));
}
}

skins::InventoryLifecycleOwner* create_skin_inventory_owner(const SkinRuntimeFactoryBindings& b) {
    require_bindings(b);
    require_fresh({&b.settings.weapons});
    auto* storage = allocate_owner(b.lifecycle.memory, skin_inventory_native_size,
                                   skins::inventory_owner_size());
    // Native calloc zeros the inline records before the constructor appends
    // and initializes them. Project those zeros into the same borrowed view.
    zero_descriptor(b.settings.weapons);
    for (auto& record : b.settings.weapons.value)
        record = {0, 0, 0, 0, 0, 0};
    return &skins::construct_inventory_owner(storage, b.lifecycle, b.commands, b.entity_updates,
                                             b.configuration, b.inventory); //4347E82A ->434902DF
}
skins::GloveOwner* create_skin_glove_owner(const SkinRuntimeFactoryBindings& b) {
    require_bindings(b);
    auto& s = b.settings;
    require_fresh({&s.glove_t, &s.glove_ct, &s.glove_model_t, &s.glove_model_ct, &s.glove_paint_t,
                   &s.glove_paint_ct});
    auto* storage =
        allocate_owner(b.lifecycle.memory, skin_glove_native_size, skins::glove_owner_size());
    zero_descriptor(s.glove_t);
    zero_descriptor(s.glove_ct);
    zero_descriptor(s.glove_model_t);
    zero_descriptor(s.glove_model_ct);
    zero_descriptor(s.glove_paint_t);
    zero_descriptor(s.glove_paint_ct);
    return &skins::construct_glove_owner(storage, {b.lifecycle, b.entity_updates}, b.configuration,
                                         b.cosmetics);
}
skins::MaskOwner* create_skin_mask_owner(const SkinRuntimeFactoryBindings& b) {
    require_bindings(b);
    auto& s = b.settings;
    require_fresh({&s.mask_t, &s.mask_ct, &s.mask_model_t, &s.mask_model_ct});
    auto* storage =
        allocate_owner(b.lifecycle.memory, skin_mask_native_size, skins::mask_owner_size());
    zero_descriptor(s.mask_t);
    zero_descriptor(s.mask_ct);
    zero_descriptor(s.mask_model_t);
    zero_descriptor(s.mask_model_ct);
    return &skins::construct_mask_owner(storage, {b.lifecycle, b.entity_updates}, b.configuration,
                                        b.cosmetics);
}
void* create_skin_agent_owner(const SkinRuntimeFactoryBindings& b) {
    require_bindings(b);
    auto& s = b.settings;
    require_fresh({&s.agent_t, &s.agent_ct, &s.agent_model_t, &s.agent_model_ct});
    auto* storage =
        allocate_owner(b.lifecycle.memory, skin_agent_native_size, skins::agent_owner_size());
    zero_descriptor(s.agent_t);
    zero_descriptor(s.agent_ct);
    zero_descriptor(s.agent_model_t);
    zero_descriptor(s.agent_model_ct);
    skins::construct_agent_owner(storage, b.lifecycle, b.configuration, b.agent);
    return storage;
}
void bind_skin_runtime_factories(NativeStartupSources& sources,
                                 SkinRuntimeFactoryBindings& bindings) {
    require_bindings(bindings);
    if (&sources.data() != &bindings.entity_updates.image())
        throw std::invalid_argument("Skin factories must share the startup canonical data owner");
    const std::array calls{
        NativeInitializerCall{skin_inventory_factory, &bindings, inventory_entry},
        NativeInitializerCall{skin_glove_factory, &bindings, glove_entry},
        NativeInitializerCall{skin_mask_factory, &bindings, mask_entry},
        NativeInitializerCall{skin_agent_factory, &bindings, agent_entry}};
    for (const auto& call : calls)
        sources.bind_initializer(call);
}
} //namespace skeet::client
