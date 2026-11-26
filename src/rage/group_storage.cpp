#include "skeet/rage/group_storage.hpp"
#include "skeet/config_storage/storage.hpp"

namespace skeet::rage {
namespace {
// Both orders are native pointer-table orders, established independently of
// their scattered global addresses and ordinary CRT callback order.
constexpr std::array<std::uint32_t, 12> blob_addresses{
    //43472B98
    0x43478F58, 0x43479018, 0x43478EF8, 0x43478FF8, 0x43478F78, 0x43478F38,
    0x43479058, 0x43479038, 0x43478F98, 0x43478FD8, 0x43478F18, 0x43478FB8};
constexpr std::array<std::uint32_t, 12> blob_hashes{0x210D3A72, 0x9599E10A, 0xADB1D494, 0xBDCCCC63,
                                                    0x2BB92C57, 0x935CA480, 0x2C3F27C1, 0x0D4168C1,
                                                    0x056F4E4F, 0x45F8FEC5, 0x85D3154A, 0x6B8A7FCE};
NativeBlobBytes bytes(engine::NativeDataImage& image, engine::JobMemory memory, std::size_t index) {
    return {image.native_cell<NativeBlobCells>(blob_addresses[index] + 0x10), memory,
            image.native_cell<std::byte>(0x434759AC)};
}
}
GroupStorage::GroupStorage(engine::PreservedInitialization& initialization,
                           engine::JobMemory memory)
    : GroupStorage(initialization.image_owner(), memory) {}
GroupStorage::GroupStorage(std::shared_ptr<engine::NativeDataImage> image, engine::JobMemory memory)
    : image_(image), native_{bytes(engine::require_native_data_image(image_), memory, 0),
                             bytes(*image_, memory, 1),
                             bytes(*image_, memory, 2),
                             bytes(*image_, memory, 3),
                             bytes(*image_, memory, 4),
                             bytes(*image_, memory, 5),
                             bytes(*image_, memory, 6),
                             bytes(*image_, memory, 7),
                             bytes(*image_, memory, 8),
                             bytes(*image_, memory, 9),
                             bytes(*image_, memory, 10),
                             bytes(*image_, memory, 11)},
      enabled_pointers_(enabled_.descriptors()) {
    for (std::size_t index = 0; index < blobs_.size(); ++index) {
        blobs_[index].value.bind_native(native_[index]);
        blob_pointers_[index] = &blobs_[index];
    }
}
void GroupStorage::initialize_blob(std::size_t index, ConfigLink& registry) {
    // The semantic view borrows native+10's pointer triple. Register its real
    // adjacent header, so serialization/menu lookup and terminal cleanup all
    // refer to the same object instead of treating ConfigBlob as raw storage.
    ConfigRegistries::for_descriptors(registry).bind_blob(
        image_->native_cell<ConfigDescriptor>(blob_addresses[index]), blobs_[index]);
    initialize_blob_config(registry, blobs_[index], blob_hashes[index]);
}
void GroupStorage::initialize_enabled(std::size_t index, ConfigLink& registry) noexcept {
    auto& descriptor = enabled_.values[index];
    append_config_link(registry, descriptor); //metadata already exists in the image
    descriptor.value = index == 0 ? 1 : 0;
}
void GroupStorage::finalize_blob(std::size_t index) {
    native_[index].terminal_release();
}
}
