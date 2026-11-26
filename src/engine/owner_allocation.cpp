#include "skeet/engine/owner_allocation.hpp"
#include <cstring>
#include <limits>
#include <new>

namespace skeet::engine {
void* allocate_owner_storage(JobMemory memory, std::size_t owner_size) {
    if (owner_size > std::numeric_limits<std::uint32_t>::max() - 0x20u)
        throw std::bad_alloc();
    const auto total = static_cast<std::uint32_t>(owner_size + 0x20u);
    auto* allocation = memory.allocate(memory.process_heap, 8, total);
    if (!allocation)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (address + 15u) & ~std::uintptr_t{15};
    const auto displacement = static_cast<std::uint32_t>(header - address);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &total, 4);
    return reinterpret_cast<void*>(header + 16);
}
void release_owner_storage(JobMemory memory, void* object) noexcept {
    if (!object)
        return;
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
