#include "skeet/engine/preserved_initialization.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace skeet::engine {
namespace {
template <std::size_t Size>
void require_entry(const PreservedInitializationImage& image, std::uint32_t address,
                   const std::array<unsigned char, Size>& expected) {
    const auto offset = address - PreservedInitializationImage::preferred_address;
    if (std::memcmp(image.data() + offset, expected.data(), Size))
        throw std::invalid_argument(
            "Preserved initialization entry bytes do not match the input image");
}
#if defined(_M_IX86) || defined(__i386__)
using OperandEntry = std::uint32_t(__stdcall*)(std::uint32_t);
using StartupEntry = void(__cdecl*)();
#endif
}

PreservedInitializationImage::PreservedInitializationImage(std::shared_ptr<std::byte> mapping,
                                                           std::size_t mapped_size,
                                                           std::shared_ptr<const void> environment)
    : NativeDataImage(std::move(mapping), mapped_size),
      exception_environment_(std::move(environment)) {
    if (reinterpret_cast<std::uintptr_t>(data()) != preferred_address || size() < image_size ||
        !exception_environment_)
        throw std::invalid_argument(
            "Preserved initialization requires its mapped input image and live exception environment");
}

PreservedInitialization::PreservedInitialization(
    std::shared_ptr<PreservedInitializationImage> image)
    : image_(std::move(image)) {
    if (!image_)
        throw std::invalid_argument("Preserved initialization image owner is required");
    // These bytes are identical in supplied skeet.bin and the immutable
    // analysis image. No exception trap or VM jump is patched here.
    require_entry(*image_, 0x43493903,
                  std::array<unsigned char, 7>{0xcc, 4, 0, 0x51, 0xcc, 0x10, 0});
    require_entry(*image_, 0x43484f06, std::array<unsigned char, 5>{0xe9, 0x73, 0x8c, 0x0c, 0});
}

std::uintptr_t PreservedInitialization::operand(std::uint32_t address) {
#if defined(_M_IX86) || defined(__i386__)
    // Ordinary callers push the operand and continue without ADD ESP,4.
    // This specifies the caller-observed effective cleanup/return contract,
    // not a recovered RET4 opcode. The supplied entry is an INT3 transfer,
    // so the retained live environment must implement that contract; the
    // image bytes alone do not provide it.
    const auto entry =
        reinterpret_cast<OperandEntry>(image_->data() + (0x43493903 - image_->preferred_address));
    return entry(address);
#else
    (void)address;
    throw std::logic_error(
        "Preserved initialization requires its original x86 execution environment");
#endif
}
void PreservedInitialization::last_startup_callback() {
#if defined(_M_IX86) || defined(__i386__)
    const auto entry =
        reinterpret_cast<StartupEntry>(image_->data() + (0x43484f06 - image_->preferred_address));
    entry();
#else
    throw std::logic_error(
        "Preserved initialization requires its original x86 execution environment");
#endif
}
}
