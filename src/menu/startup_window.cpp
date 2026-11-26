#include "skeet/menu/startup_window.hpp"
#include "skeet/menu/startup_data.hpp"
#include "skeet/menu/root_menu.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/build_startup_convars.hpp"
#include "skeet/rage/group_storage.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/owner.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::ui {
namespace {
std::uintptr_t __stdcall find_game_window(const char* name, const char* title) {
    return reinterpret_cast<std::uintptr_t>(FindWindowA(name, title));
}
}
MainMenuWindowImports native_main_menu_window_imports() {
    return {find_game_window};
}

MainMenuWindowOwner::MainMenuWindowOwner(client::ClientRuntimeStorage& storage)
    : MainMenuWindowOwner(storage, native_main_menu_window_imports()) {}

MainMenuWindowOwner::MainMenuWindowOwner(client::ClientRuntimeStorage& storage,
                                         MainMenuWindowImports imports)
    : storage_(storage), window_(storage.menu_startup_data().window_events()) {
    auto& input = storage_.input_runtime();
    auto& head = storage_.data().native_cell<MenuWindow*>(0x4347691Cu);
    auto& dpi = storage_.menu_startup_data().dpi;
    if (head || input.main_menu || input.windows || dpi.windows)
        throw std::logic_error("Main menu startup requires its unconstructed process window list");
    if (!imports.find_window)
        throw std::invalid_argument("Main menu startup requires the genuine FindWindowA import");
    const auto handle =
        imports.find_window("Valve001", "Counter-Strike: Global Offensive - Direct3D 9");
    if (!handle)
        throw std::runtime_error("The main game window was not found");
    initialize_menu_window(window_, callbacks_, handle, initial_size);
    window_alive_ = true;
    input.main_menu = &window_;
    input.windows = &window_;
    dpi.windows = &window_;
    head = &window_;
}
MainMenuWindowOwner::~MainMenuWindowOwner() {
    release_controller();
    release_window_views();
    if (window_alive_)
        destroy_menu_window(window_, callbacks_);
}
void MainMenuWindowOwner::release_controller() noexcept {
    auto*& current_controller = storage_.data().native_cell<RootMenu*>(0x43476924u);
    if (controller_ && current_controller == controller_.get())
        current_controller = nullptr;
    controller_.reset();
}
void MainMenuWindowOwner::release_window_views() noexcept {
    auto& input = storage_.input_runtime();
    auto& head = storage_.data().native_cell<MenuWindow*>(0x4347691Cu);
    auto& dpi = storage_.menu_startup_data().dpi;
    if (input.main_menu == &window_)
        input.main_menu = nullptr;
    if (input.windows == &window_)
        input.windows = nullptr;
    if (dpi.windows == &window_)
        dpi.windows = nullptr;
    if (head == &window_)
        head = nullptr;
}
void MainMenuWindowOwner::release_gui(WindowTitleString& title,
                                      const WindowLifetimeImports& imports) noexcept {
    release_controller();
    if (window_alive_) {
        destroy_main_menu_window(window_, callbacks_, title, imports);
        window_alive_ = false;
    }
    release_window_views();
}
MenuStartupData& MainMenuWindowOwner::menu_data() const noexcept {
    return storage_.menu_startup_data();
}
RootMenu& MainMenuWindowOwner::compose(MainMenuFeatureOwners owners, StartupRenderer& renderer,
                                       const engine::BuildStartupConVarBindings& variables) {
    if (controller_ || storage_.data().native_cell<RootMenu*>(0x43476924u))
        throw std::logic_error("Main menu controller has already been constructed");
    registry_name_ = make_main_menu_registry_name();
    auto& menu = storage_.menu_startup_data();
    auto& config = owners.configuration;
    if (&config.native_storage() != &storage_.config_storage() ||
        &config.configuration() != &storage_.configuration() ||
        &owners.rage_groups.image() != &storage_.data() || &variables.data() != &storage_.data() ||
        &renderer.dpi() != &menu.dpi ||
        storage_.data().native_cell<RendererContainer*>(0x4347690Cu) != &renderer.renderer())
        throw std::invalid_argument("Main menu composition requires its canonical process owners");
    auto& shutdown = owners.scripts.modules().shutdown_state();
    const auto baseline_a =
        ConfigStreamView::native_vector(config.native_storage().baseline_cells());
    const auto baseline_b = ConfigStreamView::script_state(shutdown);
    auto* const engine = storage_.data().native_cell<void*>(0x4346A514u);
    RootMenuBindings bindings{config.configuration(),
                              owners.identities,
                              owners.rage_groups,
                              baseline_a,
                              baseline_b,
                              config.clipboard(),
                              owners.camera,
                              owners.miscellaneous,
                              storage_.settings_menu_storage().values(),
                              owners.settings_runtime,
                              storage_.data().native_cell<std::uint8_t>(0x43478C6Cu),
                              storage_.data().native_cell<std::uint8_t>(0x434758D4u) != 0,
                              owners.skins_catalog,
                              owners.skins_refresh,
                              owners.players,
                              owners.player_bindings,
                              config.presets(),
                              menu.script_values(),
                              owners.scripts,
                              owners.icons,
                              menu.dpi,
                              renderer.renderer(),
                              menu.root_globals(),
                              engine,
                              variables.accuracy_variable().get(),
                              storage_.input_runtime()};
    auto result = std::make_unique<RootMenu>(window_, bindings);
    storage_.config_storage().bind_source_menu_registry(*result, registry_name_);
    controller_ = std::move(result);
    storage_.data().native_cell<RootMenu*>(0x43476924u) = controller_.get();
    config.bind_menu(*controller_, owners.scripts.modules().manager());
    return *controller_;
}
RootMenu& MainMenuWindowOwner::controller() const {
    if (!controller_)
        throw std::logic_error("Main menu controller has not been composed");
    return *controller_;
}
} //namespace skeet::ui
