#include "skeet/engine/source_initialization_imports.hpp"
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
FirstStartupServiceBindings
SourceInitializationImports::first_startup_bindings(NativeDataImage& data) const noexcept {
    return {data, modules, images, forwarded_exports.image_directory_entry, terminate_process};
}

SourceInitializationImports native_source_initialization_imports() {
    if (sizeof(void*) != 4)
        throw std::logic_error("Source initialization imports require their x86 layouts");
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto terminate = GetProcAddress(native, "ZwTerminateProcess");
    if (!terminate)
        throw std::runtime_error("ntdll!ZwTerminateProcess export is missing");

    auto forwarded = native_forwarded_export_environment();
    if (!forwarded.modules.loader || !forwarded.modules.acquire_peb_lock ||
        !forwarded.modules.release_peb_lock)
        throw std::runtime_error(
            "Source initialization requires the real loaded-module environment");
    auto images = native_image_environment();
    auto memory = native_job_memory();

    CommonControlsExports controls;
    initialize_startup_common_controls(controls, forwarded);
    if (!controls.default_subclass_proc || !controls.remove_window_subclass ||
        !controls.set_window_subclass)
        throw std::runtime_error(
            "Required common-controls exports are unavailable in already-loaded modules");
    return {memory,
            forwarded.modules,
            images,
            forwarded,
            reinterpret_cast<NativeServiceTerminateProcess>(terminate),
            controls};
#else
    throw std::runtime_error("Source initialization imports require Windows");
#endif
}
} //namespace skeet::engine
