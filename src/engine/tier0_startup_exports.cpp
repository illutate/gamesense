#include "skeet/engine/tier0_startup_exports.hpp"
#include <stdexcept>

namespace skeet::engine {
Tier0StartupExports initialize_tier0_startup_exports(NativeDataImage& data,
                                                     const LoadedModuleEnvironment& modules,
                                                     NativeImageEnvironment images) {
    if (!images.image_nt_header)
        throw std::invalid_argument(
            "Tier0 startup exports require the genuine native image-header import");
    auto* const module =
        static_cast<std::byte*>(find_loaded_module(modules, 0xBBFFA7E0u, true, true));
    if (!module)
        throw std::runtime_error("Tier0 startup export loop requires its already loaded module");
    data.native_cell<Tier0FloatTime>(0x43468C84u) =
        reinterpret_cast<Tier0FloatTime>(resolve_pe_export(module, 0xB3D93471u, 0, true, images));
    data.native_cell<void**>(0x43467204u) =
        reinterpret_cast<void**>(resolve_pe_export(module, 0xD708420Au, 0, true, images));
    data.native_cell<Tier0MainThreadPredicate>(0x43476920u) =
        reinterpret_cast<Tier0MainThreadPredicate>(
            resolve_pe_export(module, 0x245D80A2u, 0, true, images));
    // The VM performs this dereference only after the three-row loop finishes.
    auto* const allocator_export = data.native_cell<void**>(0x43467204u);
    if (!allocator_export)
        throw std::runtime_error(
            "Tier0 g_pMemAlloc export is required by the native startup dereference");
    data.native_cell<void*>(0x43467204u) = *allocator_export;
    return {data.native_cell<Tier0FloatTime>(0x43468C84u), data.native_cell<void*>(0x43467204u),
            data.native_cell<Tier0MainThreadPredicate>(0x43476920u)};
}
} // namespace skeet::engine
