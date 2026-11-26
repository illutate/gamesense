#include "skeet/client/shutdown.hpp"
#include "skeet/misc/unload.hpp"
#include "skeet/render/backend_container.hpp"
#include "skeet/menu/startup_window.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C" {
skeet::client::ShutdownImports::UnmapSection skeet_shutdown_unmap = nullptr;
}

namespace skeet::client {
namespace {
ShutdownRuntime* active_runtime = nullptr;
int WINAPI native_kill_timer(std::uintptr_t window, std::uintptr_t timer) {
    return KillTimer(reinterpret_cast<HWND>(window), static_cast<UINT_PTR>(timer));
}
}
ShutdownImports native_shutdown_imports() {
    const auto native = GetModuleHandleA("ntdll.dll");
    const auto query = native ? GetProcAddress(native, "ZwQueryVirtualMemory") : nullptr;
    const auto unmap = native ? GetProcAddress(native, "ZwUnmapViewOfSection") : nullptr;
    if (!query || !unmap)
        throw std::runtime_error("Required native finalization export is missing");
    return {&native_kill_timer, reinterpret_cast<ShutdownImports::QueryMemory>(query),
            reinterpret_cast<ShutdownImports::UnmapSection>(unmap)};
}
void ShutdownRuntime::destroy_gui() { // 0x43336C51
    input.windows = nullptr;
    if (input.main_menu) {
        if (source_main_window)
            source_main_window->release_gui(window_title, window_imports);
        else
            ui::destroy_main_menu_window(*input.main_menu, *main_callbacks, window_title,
                                         window_imports);
        input.main_menu = nullptr;
    }
    if (renderer) {
        renderer->destroy();
        if (source_renderer_cell && *source_renderer_cell == renderer)
            *source_renderer_cell = nullptr;
        renderer = nullptr;
    }
    input.destroy_cursor();
}
void* ShutdownRuntime::finalize() { // 0x433C87F6
    restore_engine_dispatch();
    script::notify_shutdown(scripts.bridge);
    scripting_enabled = false;
    script::destroy_runtime(scripts);
    if (source_main_window)
        source_main_window->release_controller();
    release_cleanup_objects();
    destroy_gui();
    exit_callbacks.run();
    MemoryRegion region{};
    (void)imports.query_memory(reinterpret_cast<void*>(~std::uintptr_t(0)),
                               reinterpret_cast<const void*>(&finalize_client), 0, &region,
                               sizeof(region), nullptr);
    return region.allocation_base;
}
void ShutdownRuntime::detach() { // 0x433EF91A
    (void)__atomic_exchange_n(&input.disabled, 1, __ATOMIC_SEQ_CST);
    hooks.state.unloading = true; // 433EF926, before cursor/engine/Lua teardown
    input.release_cursor();
    input.destroy_cursor();
    if (input.main_menu) {
        input.main_menu->transition_state = 0;
        input.main_menu->input.mouse_flags &= ~std::uint32_t(1);
    }
    hooks.cleanup();
    script::notify_shutdown(scripts.bridge);
    scripting_enabled = false;
    script::destroy_runtime(scripts);
    hooks.restore_window();
    restore_engine_dispatch();
    if (renderer && renderer->backend_if_alive())
        renderer->release_context();
    if (source_main_window)
        source_main_window->release_controller();
    release_cleanup_objects();
    destroy_gui();
    exit_callbacks.run();
}
void bind_shutdown_runtime(ShutdownRuntime& runtime) noexcept {
    active_runtime = &runtime;
    skeet_shutdown_unmap = runtime.imports.unmap_section;
}
ShutdownRuntime* bound_shutdown_runtime() noexcept {
    return active_runtime;
}
void unbind_shutdown_runtime(ShutdownRuntime& runtime) noexcept {
    if (active_runtime == &runtime) {
        active_runtime = nullptr;
        skeet_shutdown_unmap = nullptr;
    }
}
void* finalize_client() {
    return active_runtime->finalize();
}
void detach_client() {
    active_runtime->detach();
}
}

extern "C" void* skeet_shutdown_timer_finalize() {
    auto& runtime = *skeet::client::bound_shutdown_runtime();
    runtime.cancel_timer();
    return skeet::client::finalize_client();
}

namespace skeet::misc {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((naked)) void __stdcall unload_timer_callback(void*, std::uint32_t, std::uintptr_t,
                                                            std::uint32_t) {
#if defined(__x86_64__)
    __asm__("subq $40, %rsp\n\t"
            "call skeet_shutdown_timer_finalize\n\t"
            "addq $40, %rsp\n\t"
            "movq %rax, %rdx\n\t"
            "movq $-1, %rcx\n\t"
            "jmp *skeet_shutdown_unmap(%rip)");
#elif defined(__i386__)
    __asm__("call _skeet_shutdown_timer_finalize\n\t"
            "popl %edx\n\t"
            "addl $16, %esp\n\t"
            "pushl %eax\n\t"
            "pushl $-1\n\t"
            "pushl %edx\n\t"
            "jmp *_skeet_shutdown_unmap");
#else
#error The unload tail jump requires an x86 or x64 target.
#endif
}
#elif defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __stdcall unload_timer_callback(void*, std::uint32_t, std::uintptr_t,
                                                       std::uint32_t) {
    __asm {
        call skeet_shutdown_timer_finalize
        pop edx
        add esp,16
        push eax
        push -1
        push edx
        jmp dword ptr [skeet_shutdown_unmap]
    }
}
#else
#error The unload callback needs a compiler supporting the verified x86/x64 tail-jump body.
#endif
}
