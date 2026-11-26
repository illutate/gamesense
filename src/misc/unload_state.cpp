#include "skeet/misc/unload.hpp"
#include "skeet/script/manager.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::misc {
namespace {
std::uintptr_t set_timer_import(std::uintptr_t window, std::uintptr_t identifier,
                                std::uint32_t interval, UnloadTimerCallback callback) {
    return SetTimer(reinterpret_cast<HWND>(window), identifier, interval,
                    reinterpret_cast<TIMERPROC>(callback));
}
}
UnloadImports::UnloadImports() : set_timer(set_timer_import) {}

void UnloadRuntime::refresh_bound_settings() { // 0x433AB1C2 / 0x4337EABC
    const auto stream = current_config.bytes(); // capture original ECX/EDX range before callbacks
    if (stream.empty())
        return;
    if (source_ui_) {
        script::restore_script_ui_values(*source_ui_, stream, source_preserve_toggle_);
        return;
    }
    for (const auto& binding : config_bindings) {
        if (binding.disabled || !binding.descriptor)
            continue;
        const bool changed =
            deserialize_config_value(*binding.descriptor, stream, *preserve_hotkey_toggle != 0);
        if (binding.widget) {
            binding.widget->config_loaded();
            if (changed)
                binding.widget->value_changed();
        }
    }
}
} // namespace skeet::misc
