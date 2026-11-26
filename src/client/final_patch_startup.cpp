#include "skeet/client/final_patch_startup.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/player_receive_owner.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/engine/client_class_factory_hooks.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/engine/build_final_startup_bindings.hpp"
#include "skeet/network/incoming_update.hpp"
#include "skeet/network/startup_user_messages.hpp"
#include "skeet/network/receive_float_proxy.hpp"
#include "skeet/effects/visual_receive_owner.hpp"
#include "skeet/skins/runtime_owner.hpp"
#include "skeet/skins/agent_receive.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <stdexcept>
#include <vector>

namespace skeet::client {
void initialize_final_feature_callbacks(ClientRuntimeStorage& storage) {
    auto& owners = storage.owner_registries().at(0x43475B78u);
    auto entries = owners.entries();
    if (entries.size() != 57)
        throw std::logic_error("Final initialization requires all57 actual feature owners");
    for (auto* owner : entries)
        engine::invoke<void>(owner, 1);
    if (engine::invoke<bool>(storage.data().native_cell<void*>(0x4346A514u), 26)) {
        entries = owners.entries();
        for (auto* owner : entries)
            engine::invoke<void>(owner, 5);
    }
}
void capture_final_startup_patches(ClientRuntimeStorage& storage, PlayerReceiveOwner& players,
                                   effects::VisualReceiveOwner& visual,
                                   skins::SkinsRuntimeOwner& skins,
                                   engine::ClientClassFactoryRuntime& factories) {
    if (&players.storage() != &storage || &visual.storage() != &storage)
        throw std::invalid_argument("Final receive patches require canonical source runtimes");
    auto& image = storage.data();
    auto*& head = image.native_cell<EntityUpdatePatch*>(0x43476908u);
    if (head)
        throw std::logic_error("Final startup patch list is already constructed");
    auto* const base_client = image.native_cell<void*>(0x434683E0u);
    if (!base_client)
        throw std::logic_error("Final startup requires actual VClient018");
    struct Capture {
        EntityUpdatePatch* node;
        volatile std::int32_t* target;
        std::int32_t original, replacement;
    };
    std::vector<Capture> captures;
    captures.reserve(31);
    const auto capture = [&](std::uint32_t address, volatile std::int32_t* target,
                             std::uintptr_t replacement) {
        auto& node = image.native_cell<EntityUpdatePatch>(address);
        if (node.next || node.target || node.after || node.before)
            throw std::logic_error("A final startup patch is already initialized");
        if (!target || !*target || !replacement)
            throw std::runtime_error(
                "A final startup patch target or original callback is missing");
        captures.push_back(
            {&node, target, *target, std::bit_cast<std::int32_t>(std::uint32_t(replacement))});
    };
    for (const auto& row : engine::client_class_factory_descriptors()) {
        auto* const client = engine::find_client_class(base_client, row.class_hash);
        if (!client)
            throw std::runtime_error("Required final ClientClass factory is missing");
        capture(row.patch_address, reinterpret_cast<volatile std::int32_t*>(&client->create),
                reinterpret_cast<std::uintptr_t>(row.replacement));
    }
    const auto receive = [&](const auto& row) {
        auto* const property = engine::find_receive_property(base_client, row.class_hash,
                                                             row.table_hash, row.property_hash);
        if (!property)
            throw std::runtime_error("Required final receive property is missing");
        capture(row.patch_address, reinterpret_cast<volatile std::int32_t*>(&property->proxy),
                reinterpret_cast<std::uintptr_t>(row.replacement));
    };
    for (const auto& row : player_receive_descriptors())
        receive(row);
    const auto sequence = skins::sequence_receive_descriptors();
    for (const auto& row : sequence)
        receive(row);
    for (const auto& row : effects::visual_receive_descriptors())
        receive(row);
    const auto& agent = skins::agent_model_receive_descriptor();
    receive(agent);
    if (captures.size() != 31)
        throw std::logic_error("Final startup patch recipe count differs");
    // All mandatory source targets are validated before publication. This is
    // explicit source failure policy; it does not invent a native null branch.
    for (const auto& item : captures) {
        item.node->target = item.target;
        item.node->after = item.original;
        item.node->before = item.replacement;
        item.node->next = head;
        head = item.node;
    }
    engine::bind_client_class_factory_runtime(factories);
    players.bind_receive_patches();
    visual.runtime().bind_receive_patches();
    std::array<EntityUpdatePatch*, 4> sequence_nodes;
    for (std::size_t index = 0; index < sequence.size(); ++index)
        sequence_nodes[index] =
            &image.native_cell<EntityUpdatePatch>(sequence[index].patch_address);
    skins.bind_receive_patches(sequence_nodes,
                               image.native_cell<EntityUpdatePatch>(agent.patch_address));
}
void finish_final_engine_startup(ClientRuntimeStorage& storage, engine::HookRuntimeOwner& hooks) {
    if (&hooks.storage() != &storage)
        throw std::invalid_argument("Final engine startup requires its canonical hook owner");
    finish_final_engine_startup(storage, hooks.descriptor_storage(), hooks.unload_state());
}
void finish_final_engine_startup(ClientRuntimeStorage& storage,
                                 engine::HookDescriptorStorage& descriptors,
                                 engine::UnloadState& unload) {
    auto& data = storage.data();
    auto& owners = storage.owner_registries().at(0x43475B78u);
    if (&descriptors.image() != &data || &unload.owners != &owners ||
        &unload.clear_4347644c != &data.native_cell<std::uint32_t>(0x4347644Cu) ||
        &unload.clear_43476908 != &data.native_cell<std::uint32_t>(0x43476908u))
        throw std::invalid_argument(
            "Final engine startup views must borrow the canonical data and owner registry");
    auto entries = owners.entries();
    if (entries.size() != 57 || !data.native_cell<network::ClientStateScopedPatch*>(0x43476900u) ||
        !data.native_cell<EntityUpdatePatch*>(0x43476908u))
        throw std::logic_error(
            "Final engine startup requires all57 owners and both actual receive patch chains");
    auto& incoming = data.native_cell<network::IncomingUpdatePatch*>(0x4347644Cu);
    auto& fire = descriptors.at(0x43475A68u);
    auto& overlay = descriptors.at(0x4347591Cu);
    auto& state = descriptors.at(0x43475A3Cu);
    if (incoming || fire.target || fire.unknown_0 || overlay.target || state.target ||
        unload.final_patch.enabled || unload.second_patch.enabled || unload.cleared_patch.saved)
        throw std::logic_error("Final engine startup has already published its patches");
    const auto binding = engine::initialize_build_final_startup_bindings(data);
    auto* const client = data.native_cell<void*>(0x434683E0u);
    auto* const klass = engine::find_client_class(client, 0xD929B4F3u);
    if (!klass || !klass->create_event)
        throw std::runtime_error("The actual CTEFireBullets event factory is missing");
    using CreateEvent = void*(__cdecl*)();
    auto* const bullet = reinterpret_cast<CreateEvent>(klass->create_event)();
    auto* const stream = *binding.overlay_stream_slot;
    auto* const client_state = data.native_cell<void*>(0x43468E44u);
    const auto actual_receiver = [](void* receiver, const engine::VtableHookMetadata& metadata) {
        return receiver && metadata.original_table && metadata.replacement_table &&
               *static_cast<void***>(receiver) == metadata.original_table;
    };
    // The working dump can contain destroyed external UI/overlay objects.
    // Prevalidate live source receivers rather than publishing into stale
    // memory. This is explicit source failure policy, not a fabricated guard.
    if (!actual_receiver(bullet, fire) || !actual_receiver(stream, overlay) ||
        !actual_receiver(client_state, state) || !binding.float_proxy_slot ||
        !*binding.float_proxy_slot)
        throw std::runtime_error(
            "A final live engine receiver does not match its prepared RTTI descriptor");
    fire.target = bullet;
    fire.unknown_0 = reinterpret_cast<std::uintptr_t>(incoming);
    incoming = reinterpret_cast<network::IncomingUpdatePatch*>(&fire);
    overlay.target = stream;
    unload.final_patch = {static_cast<void**>(stream), overlay.original_table,
                          reinterpret_cast<std::uintptr_t>(overlay.replacement_table)};
    std::atomic_ref<void**>(*static_cast<void***>(stream)).exchange(overlay.replacement_table);
    state.target = client_state;
    unload.second_patch = {static_cast<void**>(client_state), state.original_table,
                           reinterpret_cast<std::uintptr_t>(state.replacement_table)};
    std::atomic_ref<void**>(*static_cast<void***>(client_state)).exchange(state.replacement_table);
    auto* const original = *binding.float_proxy_slot;
    data.native_cell<void*>(0x43476A44u) = original;
    network::bind_receive_float_proxy(data);
    auto* const replacement = reinterpret_cast<void*>(&network::receive_float_proxy);
    data.native_cell<void*>(0x43476A48u) = replacement;
    unload.cleared_patch = {binding.float_proxy_slot, original,
                            reinterpret_cast<std::uintptr_t>(replacement)};
    std::atomic_ref<void*>(*binding.float_proxy_slot).exchange(replacement);
    entries = owners.entries();
    for (auto* owner : entries)
        engine::invoke<void>(owner, 3);
    // Exact guest execution returns AL1 here to4355BB96. Subsequent protected
    // outer-caller checks are not part of43484F06's completed implementation.
}
} //namespace skeet::client
