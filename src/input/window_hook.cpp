#include "skeet/input/window_hook.hpp"
#include "skeet/engine/common_controls_initialization.hpp"
#include <intrin.h>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::input {
namespace {
WindowSubclassRuntime* active_runtime = nullptr;
template <class Function> Function required_export(const char* module_name, const char* name) {
    const auto module = GetModuleHandleA(module_name);
    const auto function = module ? GetProcAddress(module, name) : nullptr;
    if (!function)
        throw std::runtime_error("Required window-hook system export is missing");
    return reinterpret_cast<Function>(function);
}
std::uint64_t timestamp_counter() {
    return __rdtsc();
}
std::uintptr_t* thread_id_slot() {
    using CurrentTeb = void*(WINAPI*)();
    static const auto current = required_export<CurrentTeb>("ntdll.dll", "NtCurrentTeb");
    auto* const teb = static_cast<std::byte*>(current());
    return reinterpret_cast<std::uintptr_t*>(teb + (sizeof(void*) == 4 ? 0x24 : 0x48));
}
std::uint32_t __stdcall window_thread(std::uintptr_t window, std::uint32_t* process_id) {
    DWORD process = 0;
    const auto thread =
        GetWindowThreadProcessId(reinterpret_cast<HWND>(window), process_id ? &process : nullptr);
    if (process_id)
        *process_id = process;
    return thread;
}
}
WindowHookImports native_window_hook_imports(const engine::CommonControlsExports& common_controls) {
    if (!common_controls.set_window_subclass || !common_controls.default_subclass_proc)
        throw std::runtime_error("Common-controls startup imports are incomplete");
    return {timestamp_counter,
            thread_id_slot,
            reinterpret_cast<WindowHookImports::SetSubclass>(common_controls.set_window_subclass),
            reinterpret_cast<WindowHookImports::DefaultProcedure>(
                common_controls.default_subclass_proc),
            window_thread,
            required_export<WindowHookImports::FileHeader>("ntdll.dll", "RtlPcToFileHeader")};
}
void WindowSubclassRuntime::install(std::uintptr_t window) { // 0x433CA545
    auto& state = hooks.state.window;
    state.window = window;
    state.subclass_id = (imports.read_timestamp_counter() % 0x7fff) | 1u;
    state.restored_ansi_procedure = hooks.system.get_window_long_a(window, -4);
    state.restored_wide_procedure = hooks.system.get_window_long_w(window, -4);
    const auto thread = imports.get_window_thread(window, nullptr);
    auto* const current_thread = imports.current_thread_id_slot();
    const auto previous_thread = *current_thread;
    if (thread != previous_thread)
        *current_thread = thread;
    const auto procedure = pending_procedure;
    pending_procedure = nullptr;
    const auto installed = imports.set_window_subclass(
        window, procedure, state.subclass_id, std::uint32_t(state.restored_wide_procedure));
    if (thread != previous_thread)
        *current_thread = previous_thread;
    if (installed && hooks.system.get_window_long_a(window, -4) == state.restored_ansi_procedure &&
        hooks.system.get_window_long_w(window, -4) == state.restored_wide_procedure) {
        const auto candidate = std::uint32_t(hooks.system.get_window_long_w(state.window, -21));
        if (candidate) {
            const auto common_controls =
                engine::find_loaded_module(modules, 0xc0ba92c9, true, true);
            if (common_controls) {
                void* base = nullptr;
                if (imports.pc_to_file_header(reinterpret_cast<void*>(std::uintptr_t(candidate)),
                                              &base) &&
                    base == common_controls)
                    hooks.system.set_window_long_w(state.window, -4, std::int32_t(candidate));
            }
        }
    }
    state.expected_ansi_procedure = hooks.system.get_window_long_a(window, -4);
    state.expected_wide_procedure = hooks.system.get_window_long_w(window, -4);
    if (!installed) {
        state.window = 0;
        state.subclass_id = 0;
    }
}
std::intptr_t WindowSubclassRuntime::process(std::uintptr_t window, std::uint32_t message,
                                             std::uintptr_t parameter, std::intptr_t key_data,
                                             std::uintptr_t identifier) { // 0x4341F1DF
    if (identifier == hooks.state.window.subclass_id) {
        if (message == 0x82)
            hooks.restore_window();
        else {
            ui::WindowMessage input_message{
                window, {message, std::uint32_t(parameter), std::uint32_t(key_data)}};
            if (input.dispatch(input_message))
                return 1;
            parameter = input_message.input.parameter;
            key_data = std::int32_t(input_message.input.key_data);
        }
    }
    return imports.default_subclass_proc(window, message, parameter, key_data);
}
void bind_window_subclass_runtime(WindowSubclassRuntime& runtime) noexcept {
    active_runtime = &runtime;
    runtime.hooks.state.window.subclass_procedure =
        reinterpret_cast<void*>(window_subclass_callback);
}
std::intptr_t __stdcall window_subclass_callback(void* window, std::uint32_t message,
                                                 std::uintptr_t parameter, std::intptr_t key_data,
                                                 std::uintptr_t identifier, std::uintptr_t) {
    return active_runtime->process(reinterpret_cast<std::uintptr_t>(window), message, parameter,
                                   key_data, identifier);
}
}
