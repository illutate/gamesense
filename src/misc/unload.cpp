#include "skeet/misc/unload.hpp"
#include "skeet/debug_log.hpp"
#include "skeet/misc/settings_runtime.hpp"
namespace skeet::misc {
void UnloadRuntime::request() { // 0x433A0637
    DEBUG_LOG("UNLOAD", "UnloadRuntime::request invoked");
    if (!timer_callback)
        throw std::logic_error("Client unload profile has not been selected");
    if (!main_menu.native_window)
        throw std::invalid_argument("Client unload main window is unresolved");
    if (input->main_menu != &main_menu)
        throw std::invalid_argument("Client unload and input must share the main menu window");
    if (request_gate.allow && !request_gate.allow(request_gate.context))
        return;
    main_menu.transition_state = 0;
    main_menu.input.mouse_flags &= ~std::uint32_t(1);
    if (__atomic_exchange_n(&input->disabled, 1, __ATOMIC_SEQ_CST) == 1)
        return;
    (void)deserialize_config_registry(*config_registry, baseline_config.bytes(),
                                      *preserve_hotkey_toggle != 0);
    refresh_bound_settings();
    input->release_cursor();
    input->destroy_cursor();
    hooks->cleanup();
    const auto window = main_menu.native_window;
    if (!window)
        throw std::invalid_argument("Client unload main window was cleared during cleanup");
    (void)imports.set_timer(window, 0x332, 250, timer_callback);
}

void SettingsRuntime::request_unload() {
    unload->request();
}
} // namespace skeet::misc
