#include "skeet/engine/native_data_image.hpp"
#include <new>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
NativeDataImage::NativeDataImage(std::shared_ptr<std::byte> mapping, std::size_t size)
    : mapping_(std::move(mapping)), mapped_size_(size) {
    if (!mapping_ || mapped_size_ < image_size ||
        reinterpret_cast<std::uintptr_t>(mapping_.get()) % 16)
        throw std::invalid_argument(
            "Native data storage requires a stable aligned complete address space");
}
std::shared_ptr<NativeDataImage> NativeDataImage::create_source() {
    if (sizeof(void*) != 4)
        throw std::logic_error("Source native data storage requires x86 pointers");
#if defined(_WIN32)
    // VirtualAlloc supplies zeroed writable data without executable access.
    // Source initializers must still construct their real tables and owners.
    auto* const bytes = static_cast<std::byte*>(
        VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!bytes)
        throw std::bad_alloc{};
    std::shared_ptr<std::byte> mapping(
        bytes, [](std::byte* allocation) { (void)VirtualFree(allocation, 0, MEM_RELEASE); });
    return std::shared_ptr<NativeDataImage>(new NativeDataImage(std::move(mapping), image_size));
#else
    throw std::logic_error("Source native data storage requires Windows virtual memory");
#endif
}
NativeDataImage& require_native_data_image(const std::shared_ptr<NativeDataImage>& image) {
    if (!image)
        throw std::invalid_argument("Native data view requires its stable storage owner");
    return *image;
}
} //namespace skeet::engine
