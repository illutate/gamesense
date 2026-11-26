#include "skeet/animation/state_copy.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstdint>
#include <cstring>

namespace skeet::animation {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void purge(void* vector, void* allocator) {
    if (read<std::int32_t>(vector, 8) < 0)
        return;
    const auto address = read<std::uint32_t>(vector, 0);
    if (address) {
        engine::invoke<void>(allocator, 5, reinterpret_cast<void*>(std::uintptr_t(address)));
        write(vector, 0, std::uint32_t(0));
    }
    write(vector, 4, std::uint32_t(0));
}
}
void initialize_animation_state_vector(void* state) noexcept {
    for (std::size_t offset = 0x13c; offset <= 0x14c; offset += 4)
        write(state, offset, std::uint32_t(0));
}
void release_animation_state_vector(void* state, void* allocator) {
    auto* vector = static_cast<std::byte*>(state) + 0x13c;
    write(vector, 0xc, std::uint32_t(0));
    purge(vector, allocator);
    write(vector, 0x10, read<std::uint32_t>(vector, 0));
    purge(vector, allocator);
}
} // namespace skeet::animation
