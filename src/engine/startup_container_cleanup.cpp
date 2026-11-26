#include "skeet/engine/startup_container_cleanup.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/render/device_listener.hpp"
#include <new>
#include <stdexcept>
#include <type_traits>

namespace skeet::engine {
namespace {
const StartupContainerCleanup* active_cleanup = nullptr;
void destroy_module(StartupModulePrefix& module, JobMemory memory) { //4334D2C1
    release_owner_storage(memory, module.source);
    misc::destroy_native_text(module.name, memory);
}
template <std::uint32_t Finalizer> void register_finalizer(void* opaque) {
    static_cast<StartupContainerCleanup*>(opaque)->append_finalizer(Finalizer);
}
}

void finalize_startup_owned_pointers(StartupOwnedPointers& vector, JobMemory memory) {
    auto* const end = vector.end;
    auto* cursor = vector.begin;
    if (cursor != end) {
        do {
            if (*cursor) {
                release_owner_storage(memory, *cursor);
                *cursor = nullptr; //after the heap call, including its failure return
            }
        } while (++cursor != end);
        cursor = vector.begin; //native reload after all element destruction
    }
    release_owner_storage(memory, cursor);
}
void finalize_startup_owned_texts(StartupOwnedTexts& vector, JobMemory memory) {
    auto* const end = vector.end;
    auto* cursor = vector.begin;
    if (cursor != end) {
        do {
            misc::destroy_native_text(*cursor, memory);
        } while (++cursor != end);
        cursor = vector.begin;
    }
    release_owner_storage(memory, cursor);
}
void finalize_startup_owned_modules(StartupOwnedModules& vector, JobMemory memory) {
    auto* const end = vector.end;
    auto* cursor = vector.begin;
    if (cursor != end) {
        do {
            auto* const module = *cursor;
            if (module) {
                destroy_module(*module, memory);
                release_owner_storage(memory, module);
                *cursor = nullptr;
            }
        } while (++cursor != end);
        cursor = vector.begin;
    }
    release_owner_storage(memory, cursor);
}
void finalize_startup_text_buckets(StartupTextBuckets& table, JobMemory memory) {
    auto count = table.bucket_count;
    auto** const buckets = table.buckets;
    if (count) {
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* node = buckets[i];
            while (node) {
                auto* const next = node->next; //captured before either destructor call
                misc::destroy_native_text(node->text, memory);
                release_owner_storage(memory, node);
                node = next;
            }
            buckets[i] = nullptr;
        }
        count = table.bucket_count;
    }
    table.size = 0;
    if (count > 1)
        release_owner_storage(memory, table.buckets);
}
void finalize_startup_pointer_buckets(StartupPointerBuckets& table, JobMemory memory) {
    auto count = table.bucket_count;
    auto** const buckets = table.buckets;
    if (count) {
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* node = buckets[i];
            while (node) {
                auto* const next = node->next;
                release_owner_storage(memory, node);
                node = next;
            }
            buckets[i] = nullptr;
        }
        count = table.bucket_count;
    }
    table.size = 0;
    if (count > 1)
        release_owner_storage(memory, table.buckets);
}

StartupContainerCleanup::StartupContainerCleanup(StartupContainerCleanupBindings bindings)
    : bindings_(bindings) {
    if (!bindings_.memory.free || !bindings_.memory.process_heap)
        throw std::invalid_argument(
            "Startup container cleanup requires the canonical process heap");
}
const StartupContainerCleanup& StartupContainerCleanup::bound() {
    if (!active_cleanup)
        throw std::logic_error("Startup container cleanup is not bound");
    return *active_cleanup;
}
void (*StartupContainerCleanup::source_callback(std::uint32_t target) const)() {
    void (*callback)();
    switch (target) {
    case 0x4333087Cu:
        callback = preset_hotkeys;
        break;
    case 0x433D2E2Bu:
        callback = renderer_listener;
        break;
    case 0x4331ECD4u:
        callback = agent_paths_ct;
        break;
    case 0x433F4BCBu:
        callback = agent_paths_t;
        break;
    case 0x43336C3Cu:
        callback = builtin_modules;
        break;
    case 0x4334D2ABu:
        callback = required_modules;
        break;
    case 0x4335851Bu:
        callback = text_buckets;
        break;
    case 0x433ACF65u:
        callback = pointer_buckets;
        break;
    default:
        throw std::out_of_range("Unknown startup container finalizer");
    }
    if (active_cleanup && active_cleanup != this)
        throw std::logic_error("Startup container callbacks require one stable source owner");
    active_cleanup = this;
    return callback;
}
void StartupContainerCleanup::append_finalizer(std::uint32_t target) const {
    (void)bindings_.callbacks.append(source_callback(target));
}
void StartupContainerCleanup::preset_hotkeys() {
    const auto& source = bound().bindings_;
    finalize_startup_owned_pointers(source.preset_hotkeys, source.memory);
}
void StartupContainerCleanup::renderer_listener() {
    const auto& source = bound().bindings_;
    //433D0304 restores the concrete base vtable before unregistering. Reuse
    // the same source object/storage so the compiler emits its actual table;
    // no captured table pointer is installed into a C++ object.
    source.renderer_listener.restore_base_vtable();
    if (source.unload.listener_registered) {
        source.unload.listener_registered = false;
        invoke<void>(source.unload.event_manager, 21, &source.renderer_listener);
    }
}
void StartupContainerCleanup::agent_paths_ct() {
    const auto& source = bound().bindings_;
    finalize_startup_owned_texts(source.agent_paths_ct, source.memory);
}
void StartupContainerCleanup::agent_paths_t() {
    const auto& source = bound().bindings_;
    finalize_startup_owned_texts(source.agent_paths_t, source.memory);
}
void StartupContainerCleanup::builtin_modules() {
    const auto& source = bound().bindings_;
    finalize_startup_owned_modules(source.builtin_modules, source.memory);
}
void StartupContainerCleanup::required_modules() {
    const auto& source = bound().bindings_;
    finalize_startup_owned_modules(source.required_modules, source.memory);
}
void StartupContainerCleanup::text_buckets() {
    const auto& source = bound().bindings_;
    finalize_startup_text_buckets(source.text_buckets, source.memory);
}
void StartupContainerCleanup::pointer_buckets() {
    const auto& source = bound().bindings_;
    finalize_startup_pointer_buckets(source.pointer_buckets, source.memory);
}
void bind_startup_container_cleanup(client::NativeStartupSources& sources,
                                    StartupContainerCleanup& context) {
    const auto& image = sources.data();
    const auto& bindings = context.bindings();
    if (&bindings.preset_hotkeys != &image.native_cell<StartupOwnedPointers>(0x43478DA8u) ||
        &bindings.agent_paths_ct != &image.native_cell<StartupOwnedTexts>(0x43479088u) ||
        &bindings.agent_paths_t != &image.native_cell<StartupOwnedTexts>(0x43479078u) ||
        &bindings.builtin_modules != &image.native_cell<StartupOwnedModules>(0x434792E0u) ||
        &bindings.required_modules != &image.native_cell<StartupOwnedModules>(0x434792D0u) ||
        &bindings.text_buckets != &image.native_cell<StartupTextBuckets>(0x4346D700u) ||
        &bindings.pointer_buckets != &image.native_cell<StartupPointerBuckets>(0x4346DA40u))
        throw std::invalid_argument(
            "Startup container cleanup must borrow its canonical native cells");
    sources.bind_initializer({0x4337E843u, &context, register_finalizer<0x4333087Cu>});
    sources.bind_initializer({0x4331FDB2u, &context, register_finalizer<0x433D2E2Bu>});
    sources.bind_initializer({0x433C9DFEu, &context, register_finalizer<0x4331ECD4u>});
    sources.bind_initializer({0x4337FFB5u, &context, register_finalizer<0x433F4BCBu>});
    sources.bind_initializer({0x4339791Au, &context, register_finalizer<0x43336C3Cu>});
    sources.bind_initializer({0x43398F0Cu, &context, register_finalizer<0x4334D2ABu>});
    sources.bind_initializer({0x433999F0u, &context, register_finalizer<0x4335851Bu>});
    sources.bind_initializer({0x4337F097u, &context, register_finalizer<0x433ACF65u>});
}
} //namespace skeet::engine
