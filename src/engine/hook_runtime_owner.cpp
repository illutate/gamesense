#include "skeet/engine/hook_runtime_owner.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/engine/focus_overlay_hooks.hpp"
#include "skeet/engine/build_hook_initialization.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/startup_bindings.hpp"
#include "skeet/client/misc_runtime_owner.hpp"
#include "skeet/animation/fix_runtime_owner.hpp"
#include "skeet/animation/fix_animation_backup.hpp"
#include "skeet/misc/auto_accept_owner.hpp"
#include "skeet/misc/ping_spike_channel.hpp"
#include "skeet/misc/network_console_handlers.hpp"
#include "skeet/misc/runtime.hpp"
#include "skeet/effects/player_auxiliary_hooks.hpp"
#include "skeet/network/client_state_runtime_owner.hpp"
#include "skeet/menu/root_menu.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/client_api.hpp"
#include "skeet/script/owner.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::engine {
namespace {
client::ClientRuntimeStorage& checked_network_storage(client::CrtRuntimeOwner& crt,
                                                      client::MiscRuntimeOwner& owner) {
    auto& storage = crt.storage();
    auto& data = storage.data();
    auto& feature = owner.runtime();
    auto& descriptor = crt.descriptor_storage().at(0x43475B04);
    if (&owner.storage() != &storage || &owner.channel() != &feature.channel ||
        &owner.channel().expected_table != &descriptor.original_table ||
        &owner.channel().replacement_table != &descriptor.replacement_table ||
        &owner.channel().installed_channel != &descriptor.target ||
        &owner.auto_accept_patch().metadata !=
            &crt.descriptor_storage().at(misc::auto_accept_hook_descriptor) ||
        &feature.interfaces.local_player != &data.native_cell<void*>(0x43475808) ||
        feature.interfaces.engine != data.native_cell<void*>(0x4346A514) ||
        feature.ping_spike.latency_4347a508 != &data.native_cell<double>(0x4347A508) ||
        feature.ping_spike.latency_4347b120 != &data.native_cell<double>(0x4347B120))
        throw std::invalid_argument(
            "Channel hooks must borrow the canonical misc feature and descriptor state");
    if (!data.native_cell<misc::PingSpikeNetworkClock>(0x43468C84) ||
        !data.native_cell<const float*>(0x4346A334))
        throw std::invalid_argument(
            "Channel hooks require the actual tier0 clock and engine-time alias");
    return storage;
}
struct NetworkContexts {
    client::ClientRuntimeStorage& storage;
    client::MiscRuntimeOwner& miscellaneous;
    misc::PingSpikeChannelRuntime channel;
    misc::NetworkMessageRuntime messages;
    NetworkContexts(client::CrtRuntimeOwner& crt, client::MiscRuntimeOwner& owner)
        : storage(checked_network_storage(crt, owner)), miscellaneous(owner),
          channel(owner.runtime().ping_spike,
                  {storage.data().native_cell<misc::PingSpikeNetworkHistory>(0x4347A500),
                   storage.data().native_cell<std::uint32_t>(0x4347C9F4),
                   storage.data().native_cell<float>(0x43476928),
                   storage.data().native_cell<std::array<client::StartupAcknowledgement, 150>>(
                       0x43475BE0),
                   storage.data().native_cell<void*>(0x4346A514),
                   storage.data().native_cell<misc::PingSpikeNetworkClock>(0x43468C84)}),
          messages{storage.owner_registries().at(0x43475B94),
                   {storage.data().native_cell<std::uint8_t>(0x43475BD2),
                    storage.data().native_cell<const std::int32_t*>(0x434758D8),
                    storage.data().native_cell<std::uint8_t>(0x4347C9F0),
                    storage.data().native_cell<std::int32_t>(0x4347C9EC),
                    storage.data().native_cell<float>(0x43479924),
                    storage.data().native_cell<const float*>(0x4346A334),
                    storage.data().native_cell<void (*)()>(0x43468BF0)},
                   storage.data().native_cell<const std::byte*>(0x4346A55C)} {}
};
}
struct HookRuntimeOwner::Data {
    client::CrtRuntimeOwner& crt;
    script::SourceScriptRuntimeOwner& scripts;
    ui::StartupRenderer& renderer;
    VtableHookRegistry registry;
    misc::AutoAcceptPatchState auto_accept_patch;
    misc::AutoAcceptHookFamily auto_accept_hooks;
    script::ConsoleInputHook console;
    std::unique_ptr<NetworkContexts> network;
    std::unique_ptr<BuildHookCallbacks> callbacks;
    std::unique_ptr<effects::PlayerAuxiliaryHooks> auxiliary;
    std::unique_ptr<network::ClientStateRuntimeOwner> client_state;
    std::unique_ptr<FocusOverlayHooks> focus;

    Data(client::CrtRuntimeOwner& crt_owner, script::SourceScriptRuntimeOwner& script_owner,
         ui::StartupRenderer& renderer_owner)
        : crt(crt_owner), scripts(script_owner), renderer(renderer_owner),
          registry{script_owner.unload_state().hooks, crt.storage().memory()},
          auto_accept_patch{crt.descriptor_storage().at(misc::auto_accept_hook_descriptor)},
          auto_accept_hooks(
              auto_accept_patch, registry,
              crt.storage().data().native_cell<std::byte*>(misc::auto_accept_hook_source_global),
              crt.descriptors().images(), crt.hook_system()),
          console(crt.descriptor_storage(), script_owner.bridge()) {
        auto& storage = crt.storage();
        auto& image = storage.data();
        if (&scripts.storage() != &storage || &scripts.ui_types() != &crt.ui_types() ||
            image.native_cell<ui::RendererContainer*>(0x4347690C) != &renderer.renderer() ||
            &renderer.dpi() != &storage.menu_startup_data().dpi ||
            &crt.descriptor_storage().image() != &image)
            throw std::invalid_argument(
                "Hook runtime must borrow the canonical script, renderer and descriptor owners");
    }
};
HookRuntimeOwner::HookRuntimeOwner(client::CrtRuntimeOwner& crt,
                                   script::SourceScriptRuntimeOwner& scripts,
                                   ui::StartupRenderer& renderer)
    : data_(std::make_unique<Data>(crt, scripts, renderer)) {}
HookRuntimeOwner::~HookRuntimeOwner() = default;
VtableHookRegistry& HookRuntimeOwner::registry() noexcept {
    return data_->registry;
}
client::ClientRuntimeStorage& HookRuntimeOwner::storage() const noexcept {
    return data_->crt.storage();
}
UnloadState& HookRuntimeOwner::unload_state() const noexcept {
    return data_->scripts.unload_state();
}
HookDescriptorStorage& HookRuntimeOwner::descriptor_storage() noexcept {
    return data_->crt.descriptor_storage();
}
HookDescriptorSystem HookRuntimeOwner::descriptor_system() const noexcept {
    return data_->crt.hook_system();
}
NativeImageEnvironment HookRuntimeOwner::descriptor_images() const noexcept {
    return data_->crt.descriptors().images();
}
misc::AutoAcceptPatchState& HookRuntimeOwner::auto_accept_patch() noexcept {
    return data_->auto_accept_patch;
}
misc::AutoAcceptHookFamily& HookRuntimeOwner::auto_accept_hooks() noexcept {
    return data_->auto_accept_hooks;
}
script::ConsoleInputHook& HookRuntimeOwner::console_input() noexcept {
    return data_->console;
}
script::ScriptOwner& HookRuntimeOwner::script_owner() const {
    return data_->scripts.require_owner();
}
void HookRuntimeOwner::initialize_network(client::MiscRuntimeOwner& miscellaneous) {
    if (data_->network)
        throw std::logic_error("The canonical channel hook contexts already exist");
    data_->network = std::make_unique<NetworkContexts>(data_->crt, miscellaneous);
}
misc::PingSpikeChannelRuntime& HookRuntimeOwner::channel() {
    if (!data_->network)
        throw std::logic_error("The actual misc owner has not initialized channel hooks");
    return data_->network->channel;
}
client::ChannelHookState& HookRuntimeOwner::channel_state() {
    if (!data_->network)
        throw std::logic_error("The actual misc owner has not initialized channel hooks");
    return data_->network->miscellaneous.channel();
}
misc::NetworkMessageRuntime& HookRuntimeOwner::messages() {
    if (!data_->network)
        throw std::logic_error("The actual misc owner has not initialized network messages");
    return data_->network->messages;
}
void HookRuntimeOwner::initialize_callbacks(animation::FixRuntimeOwner& animations) {
    if (data_->callbacks)
        throw std::logic_error("The canonical build hook callbacks already exist");
    if (&animations.storage() != &storage() || &animations.backup().detach.hooks != &registry())
        throw std::invalid_argument(
            "Build hook callbacks must borrow the actual animation and hook owners");
    data_->callbacks = std::make_unique<BuildHookCallbacks>(
        descriptor_storage(), registry(), storage().offsets(), animations.animations(),
        animations.backup(), channel(), data_->renderer.frame());
}
BuildHookCallbacks& HookRuntimeOwner::callbacks() {
    if (!data_->callbacks)
        throw std::logic_error(
            "The actual animation owner has not initialized build hook callbacks");
    return *data_->callbacks;
}
void HookRuntimeOwner::initialize_player_auxiliary(const client::EngineRuntimeBindings& engine,
                                                   client::ConfigurationRuntimeOwner& configuration,
                                                   client::HistoryRuntimeOwner& history,
                                                   animation::RuntimeOwner& animation) {
    if (data_->auxiliary)
        throw std::logic_error("The canonical player auxiliary hook contexts already exist");
    data_->auxiliary = std::make_unique<effects::PlayerAuxiliaryHooks>(
        storage(), engine, configuration, history, animation, *this);
}
effects::PlayerAuxiliaryHooks& HookRuntimeOwner::player_auxiliary() {
    if (!data_->auxiliary)
        throw std::logic_error(
            "The actual history owner has not initialized auxiliary player hooks");
    return *data_->auxiliary;
}
void HookRuntimeOwner::initialize_client_state(const client::EngineRuntimeBindings& engine,
                                               client::ConfigurationRuntimeOwner& configuration,
                                               client::HistoryRuntimeOwner& history,
                                               client::CommandCaptureRuntimeOwner& commands,
                                               script::SourceScriptRuntimeOwner& scripts,
                                               ui::MainMenuWindowOwner& window) {
    if (data_->client_state)
        throw std::logic_error("The canonical ClientState hook contexts already exist");
    data_->client_state = std::make_unique<network::ClientStateRuntimeOwner>(
        storage(), engine, configuration, history, commands, *this, scripts, window);
}
network::ClientStateRuntimeOwner& HookRuntimeOwner::client_state() {
    if (!data_->client_state)
        throw std::logic_error(
            "The actual menu and history owners have not initialized ClientState hooks");
    return *data_->client_state;
}
misc::AutoAcceptOwnerBindings HookRuntimeOwner::auto_accept_owner_bindings() const {
    auto& image = data_->crt.storage().data();
    return {data_->auto_accept_patch,
            image.native_cell<misc::AutoAcceptLifecycleOwner*>(0x43475970),
            image.native_cell<void*>(0x43468E98), image.native_cell<std::byte*>(0x43469234),
            image.native_cell<void*>(0x4346D554)};
}
void HookRuntimeOwner::initialize_focus(client::PostInitializationOwner& post,
                                        client::VisualRuntimeOwner& visual) {
    if (data_->focus)
        throw std::logic_error("The canonical focus hook owner is already initialized");
    if (&post.menu().script_owner() != &data_->scripts.require_owner())
        throw std::invalid_argument(
            "Focus startup must use the hook runtime's actual script owner");
    auto focus = std::make_unique<FocusOverlayHooks>(data_->crt, post, data_->renderer, visual);
    focus->install();
    data_->focus = std::move(focus);
}
FocusOverlayHooks& HookRuntimeOwner::focus() {
    if (!data_->focus)
        throw std::logic_error(
            "The actual post-initialization focus owner has not been constructed");
    return *data_->focus;
}
}
