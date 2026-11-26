#include "skeet/client/verified_command.hpp"
#include <cstring>

namespace skeet::client {
void publish_verified_command(VerifiedCommand& destination, const Command& source,
                              CommandChecksum checksum) {
    if (&destination.command != &source)
        std::memcpy(&destination.command, &source, sizeof source);
    destination.checksum = checksum ? checksum(&destination) : 0;
}
void invalidate_command_prediction(void* prediction) noexcept {
    auto* bytes = static_cast<std::byte*>(prediction);
    constexpr std::uint32_t invalid = UINT32_MAX, zero = 0, one = 1;
    std::memcpy(bytes + 0xC, &invalid, sizeof invalid);
    std::memcpy(bytes + 0x1C, &zero, sizeof zero);
    std::memcpy(bytes + 0x24, &one, sizeof one);
}
} // namespace skeet::client
