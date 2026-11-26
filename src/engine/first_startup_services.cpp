#include "skeet/engine/first_startup_services.hpp"
#include <array>
#include <stdexcept>

namespace skeet::engine {
namespace {
constexpr std::array<FirstStartupServiceSlot, 9> slots{
    {{0x43467F6Cu, 0xE288FE8Cu, "NtOpenFile"},
     {0x4346823Cu, 0x3B0FE868u, "NtCreateFile"},
     {0x434691D4u, 0x957C7302u, "NtQueryInformationFile"},
     {0x43468C08u, 0x83B1BB6Fu, "NtQueryDirectoryFile"},
     {0x4346835Cu, 0xE0F057C2u, "NtSetInformationFile"},
     {0x43469228u, 0xC0532A13u, "NtQueryAttributesFile"},
     {0x4346A318u, 0x8DE5D376u, "NtTerminateProcess"},
     {0x43467228u, 0x5903BC5Fu, "NtMapViewOfSection"},
     {0x43468380u, 0x2C842BEEu, "NtQueryVirtualMemory"}}};
}
std::span<const FirstStartupServiceSlot, 9> first_startup_service_slots() noexcept {
    return slots;
}

void initialize_first_startup_services(const FirstStartupServiceBindings& bindings) {
    if (!bindings.modules.loader)
        throw std::invalid_argument("First startup requires the actual loaded-module list");
    const auto* entry =
        reinterpret_cast<const LoaderModule*>(bindings.modules.loader->modules.next->next);
    if (reinterpret_cast<std::uintptr_t>(entry->base) != 0x77BF0000u)
        return;
    if (!bindings.images.image_nt_header || !bindings.image_directory_entry ||
        !bindings.terminate_process)
        throw std::invalid_argument(
            "First startup requires its real native image and process imports");
    NativeServiceTable table{}; //the original zeroed3004-byte stack object
    initialize_native_service_table(table, *bindings.modules.loader, bindings.images,
                                    bindings.image_directory_entry);
    for (const auto& slot : slots) {
        auto& value = bindings.data.native_cell<std::uint32_t>(slot.address);
        value = resolve_native_service_number(table, value, bindings.terminate_process);
    }
}
} // namespace skeet::engine
