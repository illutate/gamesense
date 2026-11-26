#include "skeet/engine/unload.hpp"
#include "skeet/engine/common_controls_initialization.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
#if defined(_WIN32)
std::int32_t WINAPI native_set_long_w(std::uintptr_t window, int index, std::int32_t value) {
    return static_cast<std::int32_t>(
        SetWindowLongW(reinterpret_cast<HWND>(window), index, static_cast<LONG>(value)));
}
std::int32_t WINAPI native_set_long_a(std::uintptr_t window, int index, std::int32_t value) {
    return static_cast<std::int32_t>(
        SetWindowLongA(reinterpret_cast<HWND>(window), index, static_cast<LONG>(value)));
}
std::int32_t WINAPI native_get_long_w(std::uintptr_t window, int index) {
    return static_cast<std::int32_t>(GetWindowLongW(reinterpret_cast<HWND>(window), index));
}
std::int32_t WINAPI native_get_long_a(std::uintptr_t window, int index) {
    return static_cast<std::int32_t>(GetWindowLongA(reinterpret_cast<HWND>(window), index));
}
#endif
template <class Pointer> void exchange_pointer(Pointer* target, Pointer value) {
#if defined(_WIN32)
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(target),
                               reinterpret_cast<void*>(value));
#else
    __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
#endif
}
void restore_patch(const SavedPointerPatch& patch) {
    if (patch.enabled && patch.target)
        exchange_pointer(patch.target, patch.saved);
}
}

UnloadSystemFunctions native_unload_system_functions(const CommonControlsExports& common_controls) {
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("Required native unload module is not loaded");
    const auto remove = common_controls.remove_window_subclass;
    const auto free = GetProcAddress(native, "RtlFreeHeap");
    if (!remove || !free)
        throw std::runtime_error("Required native unload export is missing");
    UnloadSystemFunctions result;
    result.set_window_long_w = &native_set_long_w;
    result.set_window_long_a = &native_set_long_a;
    result.get_window_long_w = &native_get_long_w;
    result.get_window_long_a = &native_get_long_a;
    result.remove_window_subclass =
        reinterpret_cast<UnloadSystemFunctions::RemoveWindowSubclass>(remove);
    result.free_heap = reinterpret_cast<UnloadSystemFunctions::FreeHeap>(free);
    result.process_heap = GetProcessHeap();
    return result;
#else
    (void)common_controls;
    throw std::runtime_error("Native unload system functions require Windows");
#endif
}

void UnloadRuntime::restore_window() {
    auto& window = state.window;
    if (!window.window || !window.subclass_id)
        return;
    system.set_window_long_w(window.window, -21, window.expected_wide_procedure);
    if (!system.remove_window_subclass(window.window, window.subclass_procedure,
                                       window.subclass_id))
        return;
    if (system.get_window_long_a(window.window, -4) == window.expected_ansi_procedure)
        system.set_window_long_a(window.window, -4, window.restored_ansi_procedure);
    if (system.get_window_long_w(window.window, -4) == window.expected_wide_procedure)
        system.set_window_long_w(window.window, -4, window.restored_wide_procedure);
    window.window = 0;
    window.subclass_id = 0;
}

void free_native_allocation(void* object, const UnloadSystemFunctions& system) {
    if (!object)
        return;
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof(displacement));
    system.free_heap(system.process_heap, 0, bytes - 16 - displacement);
}

void UnloadRuntime::cleanup() {
    restore_window();
    // The production aliases hold pointer-list representations. Write their
    // bytes without accessing the pointer objects through a DWORD lvalue.
    const std::uint32_t cleared = 0;
    std::memcpy(&state.clear_43476908, &cleared, sizeof(cleared));
    std::memcpy(&state.clear_4347644c, &cleared, sizeof(cleared));
    auto& sentinel = state.hooks;
    while (sentinel.previous != &sentinel) {
        auto* node = sentinel.next;
        sentinel.next = node->next;
        sentinel.next->previous = &sentinel;
        auto** installed = *static_cast<void***>(node->object);
        const auto* metadata = static_cast<const VtableHookMetadata*>(installed[-2]);
        exchange_pointer(static_cast<void***>(node->object), metadata->original_table);
        free_native_allocation(node, system);
    }
    if (state.in_game_callback && invoke<bool>(state.engine, 27))
        state.in_game_callback();
    restore_patch(state.first_patch);
    restore_patch(state.second_patch);
    if (state.cleared_patch.target && state.cleared_patch.saved)
        exchange_pointer(state.cleared_patch.target, state.cleared_patch.saved);
    state.cleared_patch = {};
    const auto objects = state.owners.entries();
    for (auto* object : objects)
        invoke<void>(object, 4);
    restore_patch(state.final_patch);
    if (state.listener_registered) {
        state.listener_registered = false;
        invoke<void, void*>(state.event_manager, 21, state.event_listener);
    }
}

} // namespace skeet::engine
