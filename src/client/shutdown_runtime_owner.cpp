#include "skeet/client/shutdown_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/config_storage/owner.hpp"
#include "skeet/menu/startup_window.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::client {
ClientShutdownImports native_client_shutdown_imports() {
    return {native_shutdown_imports(), ui::native_window_lifetime_imports(), {}};
}
namespace {
ClientRuntimeStorage& require_owners(ClientRuntimeStorage& storage, WindowRuntimeOwner& hooks,
                                     ui::MainMenuWindowOwner& window,
                                     ConfigStorageOwner& configuration,
                                     script::SourceScriptRuntimeOwner& scripts,
                                     ui::StartupRenderer& renderer,
                                     const ClientShutdownImports& imports) {
    auto& data = storage.data();
    if (&hooks.storage() != &storage || &window.menu_data() != &storage.menu_startup_data() ||
        &configuration.native_storage() != &storage.config_storage() ||
        &configuration.configuration() != &storage.configuration() ||
        &scripts.storage() != &storage || &scripts.unload_state() != &hooks.unload_state() ||
        &renderer.dpi() != &storage.menu_startup_data().dpi ||
        data.native_cell<ui::RendererContainer*>(0x4347690Cu) != &renderer.renderer() ||
        storage.input_runtime().main_menu != &window.window() ||
        scripts.shutdown_state().ui != &scripts.ui_bindings() ||
        &scripts.shutdown_state().bridge != &scripts.bridge())
        throw std::invalid_argument("Source shutdown requires the canonical process owners");
    if (!imports.finalization.kill_timer || !imports.finalization.query_memory ||
        !imports.window.free_unicode_string || !imports.request.set_timer)
        throw std::invalid_argument("Source shutdown requires its actual lifecycle imports");
    return storage;
}
}
struct ClientShutdownOwner::Data {
    ClientRuntimeStorage& storage;
    ChannelHookState channel;
    ShutdownRuntime finalizer;
    misc::UnloadRuntime request;
    std::unique_ptr<SourceDllShutdownProfile> profile;
    Data(ClientRuntimeStorage& source, WindowRuntimeOwner& hooks, ui::MainMenuWindowOwner& window,
         ConfigStorageOwner& configuration, script::SourceScriptRuntimeOwner& scripts,
         ui::StartupRenderer& renderer, ClientShutdownImports imports)
        : storage(require_owners(source, hooks, window, configuration, scripts, renderer, imports)),
          channel(storage.data().native_cell<void**>(0x43475B0Cu),
                  storage.data().native_cell<void**>(0x43475B14u),
                  storage.data().native_cell<void*>(0x43475B08u)),
          finalizer(scripts.shutdown_state(), storage.input_runtime(), imports.finalization,
                    scripts.runtime().enabled, hooks.unload_runtime(), storage.exit_callbacks(),
                    storage.config_storage().path(), window.window(), channel),
          request(window.window(), scripts.ui_bindings(), config_loading_flag()) {
        finalizer.main_callbacks = &window.callbacks();
        finalizer.window_imports = imports.window;
        finalizer.renderer = &renderer.renderer();
        finalizer.source_main_window = &window;
        finalizer.source_renderer_cell =
            &storage.data().native_cell<ui::RendererContainer*>(0x4347690Cu);
        request.config_registry = &storage.configuration().descriptors;
        request.baseline_config =
            ConfigStreamView::native_vector(storage.config_storage().baseline_cells());
        request.current_config = ConfigStreamView::script_state(scripts.shutdown_state());
        request.input = &storage.input_runtime();
        request.hooks = &hooks.unload_runtime();
        request.imports = imports.request;
        request.timer_callback = nullptr;
    }
    ~Data() {
        unbind_shutdown_runtime(finalizer);
    }
};
ClientShutdownOwner::ClientShutdownOwner(ClientRuntimeStorage& storage, WindowRuntimeOwner& hooks,
                                         ui::MainMenuWindowOwner& window,
                                         ConfigStorageOwner& configuration,
                                         script::SourceScriptRuntimeOwner& scripts,
                                         ui::StartupRenderer& renderer)
    : ClientShutdownOwner(storage, hooks, window, configuration, scripts, renderer,
                          native_client_shutdown_imports()) {}
ClientShutdownOwner::ClientShutdownOwner(ClientRuntimeStorage& storage, WindowRuntimeOwner& hooks,
                                         ui::MainMenuWindowOwner& window,
                                         ConfigStorageOwner& configuration,
                                         script::SourceScriptRuntimeOwner& scripts,
                                         ui::StartupRenderer& renderer,
                                         ClientShutdownImports imports)
    : data_(std::make_unique<Data>(storage, hooks, window, configuration, scripts, renderer,
                                   imports)) {}
ClientShutdownOwner::~ClientShutdownOwner() = default;
ClientRuntimeStorage& ClientShutdownOwner::storage() const noexcept {
    return data_->storage;
}
ShutdownRuntime& ClientShutdownOwner::finalizer() noexcept {
    return data_->finalizer;
}
misc::UnloadRuntime& ClientShutdownOwner::request() noexcept {
    return data_->request;
}
void ClientShutdownOwner::select_source_dll(void* module) {
    select_source_dll(module, native_source_dll_shutdown_imports(), {});
}
void ClientShutdownOwner::select_source_dll(void* module, SourceDllShutdownImports imports) {
    select_source_dll(module, imports, {});
}
void ClientShutdownOwner::select_source_dll(void* module, SourceDllShutdownCompletion completion) {
    select_source_dll(module, native_source_dll_shutdown_imports(), completion);
}
void ClientShutdownOwner::select_source_dll(void* module, SourceDllShutdownImports imports,
                                            SourceDllShutdownCompletion completion) {
    if (data_->profile)
        throw std::logic_error("Source DLL shutdown is already selected");
    auto profile =
        std::make_unique<SourceDllShutdownProfile>(data_->finalizer, module, imports, completion);
    bind_source_dll_shutdown(*profile, data_->request);
    data_->profile = std::move(profile);
}
void ClientShutdownOwner::cancel_deferred_request() noexcept {
    if (data_->profile)
        data_->profile->cancel_deferred_request();
}
}
