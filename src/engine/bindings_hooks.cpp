#include "skeet/engine/hooks.hpp"
#include <cstring>
#include <new>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
VtableHookNode* allocate_node(JobMemory memory) {
    const auto size = sizeof(VtableHookNode);
    auto* allocation = memory.allocate(memory.process_heap, 8, size + 32);
    if (!allocation)
        return nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(allocation);
    const auto header = (raw + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - raw);
    const auto allocation_size = static_cast<std::uint32_t>(size + 32);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return new (reinterpret_cast<void*>(header + 16)) VtableHookNode{};
}
void free_node(VtableHookNode* node, JobMemory memory) {
    auto* object = reinterpret_cast<std::byte*>(node);
    std::uint32_t displacement;
    std::memcpy(&displacement, object - 8, 4);
    memory.free(memory.process_heap, 0, object - 16 - displacement);
}
void replace_if_current(void*** object, void** expected, void** replacement) {
#if defined(_WIN32)
    InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(object), replacement,
                                      expected);
#else
    __atomic_compare_exchange_n(object, &expected, replacement, false, __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
#endif
}
void replace(void*** object, void** replacement) {
#if defined(_WIN32)
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(object), replacement);
#else
    __atomic_exchange_n(object, replacement, __ATOMIC_SEQ_CST);
#endif
}
}

VtableHookNode* install_vtable_hook(VtableHookRegistry& registry, void* object,
                                    const VtableHookMetadata& metadata) {
    auto*** location = static_cast<void***>(object);
    replace_if_current(location, metadata.original_table, metadata.replacement_table);
    auto* node = allocate_node(registry.memory);
    if (node) {
        node->object = object;
        node->previous = &registry.sentinel;
        node->next = registry.sentinel.next;
        registry.sentinel.next->previous = node;
        registry.sentinel.next = node;
    }
    return node;
}

void uninstall_vtable_hook(VtableHookRegistry& registry, void* object) {
    for (auto* node = registry.sentinel.previous; node != &registry.sentinel;
         node = node->previous) {
        if (node->object != object)
            continue;
        node->next->previous = node->previous;
        node->previous->next = node->next;
        free_node(node, registry.memory);
        auto*** location = static_cast<void***>(object);
        const auto* metadata = static_cast<const VtableHookMetadata*>((*location)[-2]);
        replace(location, metadata->original_table);
        return;
    }
}
} // namespace skeet::engine
