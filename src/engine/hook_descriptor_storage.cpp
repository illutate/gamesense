#include "skeet/engine/hook_descriptor_storage.hpp"
#include <algorithm>
#include <stdexcept>

namespace skeet::engine {
HookDescriptorStorage::HookDescriptorStorage(PreservedInitialization& initialization)
    : HookDescriptorStorage(initialization.image_owner()) {}
HookDescriptorStorage::HookDescriptorStorage(std::shared_ptr<NativeDataImage> image)
    : image_(image) {
    (void)require_native_data_image(image_);
    if (sizeof(void*) != 4 || sizeof(VtableHookMetadata) != 0x14)
        throw std::logic_error(
            "Native hook descriptor storage requires its original x86 pointer layout");
}

VtableHookMetadata& HookDescriptorStorage::at(std::uint32_t address) const {
    if (std::find(hook_descriptor_cells.begin(), hook_descriptor_cells.end(), address) ==
        hook_descriptor_cells.end())
        throw std::out_of_range("Address is not a recovered hook descriptor cell");
    // No construction, reset, relocation or copy occurs here. Source family
    // preparation and CRT clear functions share the same canonical bytes.
    return image_->native_cell<VtableHookMetadata>(address);
}
}
