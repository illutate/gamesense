#include "skeet/engine/common_controls_initialization.hpp"
#include "skeet/engine/forwarded_exports.hpp"
#include "skeet/engine/loaded_modules.hpp"
#include <array>

namespace skeet::engine {
// Devirtualized from VM entry 0x4354DB7E: consume three ordinal/destination
// pairs, in operand order 0xEB8..0xECC. The ordinal identities also match the
// actual comctl32 export addresses stored in the working process snapshot.
void initialize_startup_common_controls(CommonControlsExports& imports,
                                        const ForwardedExportEnvironment& environment) {
    auto* module =
        static_cast<std::byte*>(find_loaded_module(environment.modules, 0xc0ba92c9u, true, true));
    if (!module)
        return;
    struct Import {
        std::uint32_t ordinal;
        std::byte** destination;
    };
    const std::array<Import, 3> entries{{{413, &imports.default_subclass_proc},
                                         {410, &imports.set_window_subclass},
                                         {412, &imports.remove_window_subclass}}};
    for (const auto& entry : entries) {
        // The native loop also stores null results and continues with the
        // following pair; failure handling belongs to the enclosing callback.
        *entry.destination =
            resolve_forwarded_export(module, 0, entry.ordinal, true, true, environment);
    }
}
}
