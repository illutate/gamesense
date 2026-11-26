#include "skeet/engine/owner_registry.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <limits>

namespace skeet::engine {
namespace {
void* allocate_pointer_storage(JobMemory memory, std::size_t size) {
    // Native43387448, with the requested pointer-vector byte count.
    if (!size)
        return nullptr;
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - address);
    const auto allocation_size = static_cast<std::uint32_t>(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free_pointer_storage(JobMemory memory, void* object) {
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}

void append_owner(OwnerRegistry& registry, void* const* value, JobMemory memory) {
    if (reinterpret_cast<std::uintptr_t>(registry.end) <
        reinterpret_cast<std::uintptr_t>(registry.limit)) {
        auto* const position = registry.end;
        registry.end = position + 1;
        *position = *value;
        return;
    }
    const auto count = (reinterpret_cast<std::uintptr_t>(registry.end) -
                        reinterpret_cast<std::uintptr_t>(registry.begin)) /
                       sizeof(void*);
    const auto capacity = count ? 2 * count : 1;
    auto** const replacement =
        static_cast<void**>(allocate_pointer_storage(memory, capacity * sizeof(void*)));
    const auto copied_count = (reinterpret_cast<std::uintptr_t>(registry.end) -
                               reinterpret_cast<std::uintptr_t>(registry.begin)) /
                              sizeof(void*);
    std::memmove(replacement, registry.begin, copied_count * sizeof(void*));
    replacement[copied_count] = *value;
    if (registry.begin)
        free_pointer_storage(memory, registry.begin);
    registry.begin = replacement;
    registry.end = replacement + copied_count + 1;
    registry.limit = replacement + capacity;
}

void* register_owner(OwnerRegistry& registry, void* object, JobMemory memory) {
    auto* value =
        reinterpret_cast<std::uintptr_t>(object) == std::numeric_limits<std::uintptr_t>::max() - 3
            ? nullptr
            : object;
    append_owner(registry, &value, memory);
    return object;
}

void dispatch_owner_slot_six(OwnerRegistry& registry) { // 43328BEA
    const auto owners = registry.entries();
    for (auto* owner : owners)
        invoke<void>(owner, 6);
}

void dispatch_owner_slot_eight(OwnerRegistry& registry) { // 433DF17B
    const auto owners = registry.entries();
    for (auto* owner : owners)
        invoke<void>(owner, 8);
}
}
