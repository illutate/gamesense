#include "skeet/client/source_dll_shutdown.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/misc/unload.hpp"
#include <exception>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C" {
skeet::client::SourceDllShutdownImports::FreeLibrary skeet_source_dll_free_library = nullptr;
}

namespace skeet::client {
namespace {
SourceDllShutdownProfile* active = nullptr;
SRWLOCK profile_lock = SRWLOCK_INIT;
struct ProfileGuard {
    ProfileGuard() {
        AcquireSRWLockExclusive(&profile_lock);
    }
    ~ProfileGuard() {
        ReleaseSRWLockExclusive(&profile_lock);
    }
};
}
SourceDllShutdownImports native_source_dll_shutdown_imports() {
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto locate = kernel ? GetProcAddress(kernel, "GetModuleHandleExW") : nullptr;
    const auto release = kernel ? GetProcAddress(kernel, "FreeLibrary") : nullptr;
    if (!locate || !release)
        throw std::runtime_error("Loaded-DLL shutdown requires the genuine loader exports");
    return {reinterpret_cast<SourceDllShutdownImports::ModuleFromAddress>(locate),
            reinterpret_cast<SourceDllShutdownImports::FreeLibrary>(release)};
}
SourceDllShutdownProfile::SourceDllShutdownProfile(ShutdownRuntime& runtime, void* source_module)
    : SourceDllShutdownProfile(runtime, source_module, native_source_dll_shutdown_imports(), {}) {}
SourceDllShutdownProfile::SourceDllShutdownProfile(ShutdownRuntime& runtime, void* source_module,
                                                   SourceDllShutdownImports imports)
    : SourceDllShutdownProfile(runtime, source_module, imports, {}) {}
SourceDllShutdownProfile::SourceDllShutdownProfile(ShutdownRuntime& runtime, void* source_module,
                                                   SourceDllShutdownCompletion completion)
    : SourceDllShutdownProfile(runtime, source_module, native_source_dll_shutdown_imports(),
                               completion) {}
SourceDllShutdownProfile::SourceDllShutdownProfile(ShutdownRuntime& runtime, void* source_module,
                                                   SourceDllShutdownImports imports,
                                                   SourceDllShutdownCompletion completion)
    : runtime_(runtime), module_(source_module), imports_(imports), completion_(completion) {
    if (!module_ || !imports_.module_from_address || !imports_.free_library ||
        !runtime.imports.kill_timer || !runtime.imports.query_memory)
        throw std::invalid_argument(
            "Loaded-DLL shutdown requires its real module and complete finalization imports");
    if (completion_.context && !completion_.complete)
        throw std::invalid_argument(
            "Loaded-DLL application completion has no destruction callback");
    void* actual = nullptr;
    constexpr std::uint32_t from_address_unchanged_refcount = 4u | 2u;
    if (!imports_.module_from_address(
            from_address_unchanged_refcount,
            reinterpret_cast<const void*>(&source_dll_unload_timer_callback), &actual) ||
        actual != module_)
        throw std::invalid_argument(
            "Loaded-DLL shutdown module does not contain its actual timer entry");
}
SourceDllShutdownProfile::~SourceDllShutdownProfile() {
    ProfileGuard guard;
    cancel_deferred_request_locked();
    if (active == this) {
        active = nullptr;
        unbind_shutdown_runtime(runtime_);
    }
}
void SourceDllShutdownProfile::cancel_deferred_request_locked() noexcept {
    const auto window = deferred_window_;
    deferred_window_ = 0;
    if (window)
        (void)runtime_.imports.kill_timer(window, 0x332);
}
void SourceDllShutdownProfile::cancel_deferred_request() noexcept {
    ProfileGuard guard;
    cancel_deferred_request_locked();
}
bool SourceDllShutdownProfile::allow_request_locked() {
    switch (completion_.readiness(completion_.context)) {
    case SourceDllShutdownReadiness::ready:
        cancel_deferred_request_locked();
        return true;
    case SourceDllShutdownReadiness::failed:
        cancel_deferred_request_locked();
        return false;
    case SourceDllShutdownReadiness::waiting:
        if (!deferred_window_) {
            deferred_window_ = request_->main_menu.native_window;
            if (!request_->imports.set_timer(deferred_window_, 0x332, 250,
                                             &source_dll_deferred_request_timer_callback)) {
                deferred_window_ = 0;
                throw std::runtime_error(
                    "Source startup unload deferral could not create its actual window timer");
            }
        }
        return false;
    }
    std::terminate();
}
void bind_source_dll_shutdown(SourceDllShutdownProfile& profile, misc::UnloadRuntime& request) {
    ProfileGuard guard;
    auto& runtime = profile.runtime();
    if (active && active != &profile)
        throw std::logic_error("Another loaded-DLL shutdown profile is already bound");
    if (request.input != &runtime.input || request.hooks != &runtime.hooks ||
        &request.main_menu.native_window != &runtime.native_window)
        throw std::invalid_argument(
            "Loaded-DLL shutdown and unload must share the actual input, hooks and main window");
    if (profile.request_ && profile.request_ != &request)
        throw std::invalid_argument("Loaded-DLL shutdown already borrows another unload request");
    bind_shutdown_runtime(runtime);
    active = &profile;
    skeet_source_dll_free_library = profile.imports().free_library;
    request.timer_callback = &source_dll_unload_timer_callback;
    profile.request_ = &request;
    if (profile.completion_.readiness)
        request.request_gate = {&profile, [](void* value) {
                                    ProfileGuard guard;
                                    // Only compare the opaque identity until its live owner has been
                                    // found under lock; a canceled profile's context can be stale.
                                    return active == value && active->allow_request_locked();
                                }};
}
void __stdcall source_dll_deferred_request_timer_callback(void*, std::uint32_t, std::uintptr_t,
                                                          std::uint32_t) {
    misc::UnloadRuntime* request = nullptr;
    {
        ProfileGuard guard;
        if (!active || !active->deferred_window_)
            return;
        if (active->allow_request_locked())
            request = active->request_;
    }
    // ready is terminal and is published only after the initializing worker's
    // last application access. Engine cleanup runs outside the profile lock.
    if (request)
        request->request();
}
}

extern "C" void* skeet_source_dll_timer_finalize() {
    void* module;
    skeet::client::SourceDllShutdownImports::FreeLibrary release;
    skeet::client::SourceDllShutdownCompletion completion;
    skeet::client::ShutdownRuntime* runtime;
    // Save both loader values before source finalizers can release their
    // ownership contexts. The finalizer's mapped-section query result does
    // not choose the HMODULE of this explicit LoadLibrary profile.
    {
        skeet::client::ProfileGuard guard;
        auto* profile = skeet::client::active;
        if (!profile)
            std::terminate();
        module = profile->module();
        release = profile->imports().free_library;
        completion = profile->completion();
        runtime = &profile->runtime();
        skeet::client::active = nullptr;
    }
    runtime->cancel_timer();
    (void)runtime->finalize();
    skeet::client::unbind_shutdown_runtime(*runtime);
    // The callback can destroy profile/runtime. All subsequent operands are
    // saved automatic values; no application object is observed afterward.
    if (completion.complete)
        completion.complete(completion.context);
    skeet_source_dll_free_library = release;
    return module;
}

namespace skeet::client {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((naked)) void __stdcall
source_dll_unload_timer_callback(void*, std::uint32_t, std::uintptr_t, std::uint32_t) {
#if defined(__i386__)
    // TIMERPROC's16 argument bytes become one FreeLibrary argument. Its RET4
    // reaches the original user32 return address with TIMERPROC's final ESP.
    __asm__("call _skeet_source_dll_timer_finalize\n\t"
            "popl %edx\n\t"
            "addl $16,%esp\n\t"
            "pushl %eax\n\t"
            "pushl %edx\n\t"
            "jmp *_skeet_source_dll_free_library");
#elif defined(__x86_64__)
    __asm__("subq $40,%rsp\n\t"
            "call skeet_source_dll_timer_finalize\n\t"
            "addq $40,%rsp\n\t"
            "movq %rax,%rcx\n\t"
            "jmp *skeet_source_dll_free_library(%rip)");
#else
#error Loaded-DLL shutdown requires the verified x86/x64 tail-jump ABI.
#endif
}
#elif defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __stdcall source_dll_unload_timer_callback(void*, std::uint32_t,
                                                                  std::uintptr_t, std::uint32_t) {
    __asm {
        call skeet_source_dll_timer_finalize
        pop edx
        add esp,16
        push eax
        push edx
        jmp dword ptr [skeet_source_dll_free_library]
    }
}
#else
#error Loaded-DLL shutdown needs a compiler supporting the verified tail jump.
#endif
}
