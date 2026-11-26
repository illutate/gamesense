#include "skeet/client/crt_startup_bindings.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/crt_initialization.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/static_crt_initialization.hpp"
#include "skeet/client/source_terminal_cleanup.hpp"
#include "skeet/animation/fix_animation_backup_cleanup.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/script/startup_initializers.hpp"
#include "skeet/engine/first_startup_services.hpp"
#include "skeet/engine/startup_vector_cleanup.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
void first_services(void* context) {
    // Devirtualized from434822C8 ->434EA5D4; actual OS imports remain explicit.
    engine::initialize_first_startup_services(
        *static_cast<engine::FirstStartupServiceBindings*>(context));
}
void bit_masks(void* context) {
    auto& data = static_cast<ClientRuntimeStorage*>(context)->data();
    initialize_native_startup_bit_masks(
        {data.native_cell<std::array<std::uint32_t, 32 * 33>>(0x43477BE0u),
         data.native_cell<std::array<std::uint32_t, 32>>(0x43477B60u)});
}
void menu_key(void* context) {
    auto& storage = *static_cast<ClientRuntimeStorage*>(context);
    input::initialize_menu_hotkey(storage.configuration(), storage.menu_hotkey());
}
void infinite_duck(void* context) {
    auto& config = *static_cast<ConfigurationRuntimeOwner*>(context);
    initialize_infinite_duck_config(config.storage().configuration(),
                                    config.movement().infinite_duck);
}
void air_duck(void* context) {
    auto& config = *static_cast<ConfigurationRuntimeOwner*>(context);
    initialize_air_duck_config(config.storage().configuration(), config.movement().air_duck);
}
void edge_jump(void* context) {
    auto& config = *static_cast<ConfigurationRuntimeOwner*>(context);
    initialize_edge_jump_hotkey(config.storage().configuration(), config.movement().edge_jump);
}
void skin_filter(void* context) {
    auto& config = *static_cast<ConfigurationRuntimeOwner*>(context);
    config.skins_settings().initialize_shared(config.storage().configuration());
}
void keys_container(void* context) {
    auto& owner = *static_cast<StaticCrtRuntimeOwner*>(context);
    auto& data = owner.storage().data();
    data.native_cell<std::uint32_t>(0x4346DAACu) =
        owner.static_initializers().keys_constructor_word;
    data.native_cell<void*>(0x4346DAB0u) = &data.native_cell<std::byte>(0x4346DAB4u);
    (void)owner.storage().exit_callbacks().append(
        owner.native_vector_cleanup().source_finalizer(0x433FBE15u));
}
}

void bind_early_crt_sources(NativeStartupSources& sources, CrtRuntimeOwner& crt,
                            ConfigurationRuntimeOwner& configuration,
                            StaticCrtRuntimeOwner& containers,
                            script::SourceScriptRuntimeOwner& scripts,
                            animation::FixAnimationBackupCleanupContext& backups,
                            SourceTerminalCleanupBindings& terminal) {
    auto& storage = crt.storage();
    if (&sources.data() != &storage.data() || &configuration.storage() != &storage ||
        &containers.storage() != &storage || &scripts.storage() != &storage ||
        &backups.callbacks != &storage.exit_callbacks() ||
        &backups.allocator != &storage.data().native_cell<void*>(0x43467204u) ||
        &terminal.callbacks != &storage.exit_callbacks() || &terminal.bridge != &scripts.bridge() ||
        &terminal.browser != &scripts.browser_state() || &terminal.ui != &scripts.ui_bindings() ||
        &terminal.strings != &scripts.flags())
        throw std::invalid_argument(
            "Early CRT must borrow the actual process storage and cleanup owners");
    if (storage.exit_callbacks().count)
        throw std::logic_error("Early CRT bindings must be selected before initialization begins");
    crt.bind_initializers(sources);
    configuration.bind_initializers(sources);
    containers.bind_initializers(sources);
    script::bind_native_startup_initializers(sources, scripts.startup_initializers());
    animation::bind_fix_animation_backup_exit_initializer(sources, backups);
    sources.bind_initializer({0x434822C8u, &crt.first_startup_services(), first_services});
    sources.bind_initializer({0x433FB71Eu, &storage, bit_masks});
    sources.bind_initializer({0x434928A2u, &storage, menu_key});
    sources.bind_initializer({0x434933A6u, &configuration, infinite_duck});
    sources.bind_initializer({0x43490B39u, &configuration, air_duck});
    sources.bind_initializer({0x4349340Bu, &configuration, edge_jump});
    sources.bind_initializer({0x43492E21u, &configuration, skin_filter});
    sources.bind_initializer({0x433412F8u, &containers, keys_container});
    // Select actual source containers after native-header binders. This also
    // supplies4332BB3A for the source reference-string vector.
    bind_source_terminal_cleanup(sources, terminal);
    preflight_crt_initialization(sources);
}
} //namespace skeet::client
