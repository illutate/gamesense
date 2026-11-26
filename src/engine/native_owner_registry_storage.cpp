#include "skeet/engine/native_owner_registry_storage.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace skeet::engine {
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(OwnerRegistry) == 0xc);
static_assert(offsetof(OwnerRegistry, begin) == 0);
static_assert(offsetof(OwnerRegistry, end) == 4);
static_assert(offsetof(OwnerRegistry, limit) == 8);
#endif

NativeOwnerRegistryStorage::NativeOwnerRegistryStorage(PreservedInitialization& initialization)
    : NativeOwnerRegistryStorage(initialization.image_owner()) {}
NativeOwnerRegistryStorage::NativeOwnerRegistryStorage(std::shared_ptr<NativeDataImage> image)
    : image_(image) {
    (void)require_native_data_image(image_);
    if (sizeof(void*) != 4 || sizeof(OwnerRegistry) != 0xc)
        throw std::logic_error("Native owner registries require their original x86 pointer layout");
}

OwnerRegistry& NativeOwnerRegistryStorage::at(std::uint32_t address) const {
    if (std::find(native_owner_registry_cells.begin(), native_owner_registry_cells.end(),
                  address) == native_owner_registry_cells.end())
        throw std::out_of_range("Address is not a recovered raw owner registry triplet");
    // Viewing an existing native object must not invoke OwnerRegistry's
    // default member initializers or allocate/copy its pointer buffer.
    return image_->native_cell<OwnerRegistry>(address);
}

}
