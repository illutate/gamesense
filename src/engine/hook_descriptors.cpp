#include "skeet/engine/hook_descriptors.hpp"
#include <cstring>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
HookDescriptorRuntime* active_runtime = nullptr;
void* allocate_table(JobMemory memory, std::size_t size) {
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
void free_table(JobMemory memory, void* object) {
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}

HookDescriptorSystem native_hook_descriptor_system() {
#if defined(_WIN32)
    const auto native = GetModuleHandleA("ntdll.dll");
    if (!native)
        throw std::runtime_error("ntdll.dll is not loaded");
    const auto query = GetProcAddress(native, "ZwQueryVirtualMemory");
    if (!query)
        throw std::runtime_error("ZwQueryVirtualMemory is not available");
    return {reinterpret_cast<HookQueryMemory>(query)};
#else
    throw std::runtime_error("Native hook descriptor queries require Windows");
#endif
}

void prepare_vtable_hook(VtableHookMetadata& metadata, void** original_table, JobMemory memory,
                         HookDescriptorSystem system) { // 433AB42B
    if (!original_table)
        return;
    std::size_t count = 0;
    while (original_table[count]) {
        HookMemoryRegion region;
        const auto status =
            system.query_memory(reinterpret_cast<void*>(~std::uintptr_t(0)), original_table[count],
                                0, &region, sizeof(region), nullptr);
        if (status < 0 || !(region.protection & 0xf0u))
            break;
        ++count;
    }
    if (!count)
        return;
    auto** const allocation =
        static_cast<void**>(allocate_table(memory, (count + 2) * sizeof(void*)));
    if (!allocation)
        return;
    allocation[0] = &metadata;
    std::memcpy(allocation + 1, original_table - 1, (count + 1) * sizeof(void*));
    metadata.original_table = original_table;
    metadata.replacement_table = allocation + 2;
}

void clear_vtable_hook(VtableHookMetadata& metadata, JobMemory memory) { // 433D2DC1
    void** previous;
#if defined(_WIN32)
    previous = static_cast<void**>(InterlockedExchangePointer(
        reinterpret_cast<void* volatile*>(&metadata.replacement_table), nullptr));
#else
    previous = __atomic_exchange_n(&metadata.replacement_table, static_cast<void**>(nullptr),
                                   __ATOMIC_SEQ_CST);
#endif
    if (previous)
        free_table(memory, previous - 2);
}

void prepare_rtti_vtable_hook(VtableHookMetadata& metadata, std::byte* module,
                              std::uint32_t type_hash, std::uint32_t base_hash,
                              NativeImageEnvironment images, JobMemory memory,
                              HookDescriptorSystem system) {
    prepare_vtable_hook(metadata, resolve_rtti_vtable(module, type_hash, base_hash, images), memory,
                        system);
}

void bind_hook_descriptor_runtime(HookDescriptorRuntime& runtime) noexcept {
    active_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall prepare_vtable_hook_native(VtableHookMetadata* metadata, void*,
                                           void** original_table) {
#else
void prepare_vtable_hook_native(VtableHookMetadata* metadata, void*, void** original_table) {
#endif
    // Native433AB42B: ECX descriptor, one stack argument, RET4.
    active_runtime->prepare(*metadata, original_table);
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall clear_vtable_hook_native(VtableHookMetadata* metadata, void*) {
#else
void clear_vtable_hook_native(VtableHookMetadata* metadata, void*) {
#endif
    // Native433D2DC1: ECX descriptor, no stack arguments.
    active_runtime->clear(*metadata);
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_DESCRIPTOR_NATIVE __fastcall
#else
#define SKEET_DESCRIPTOR_NATIVE
#endif
const std::byte* SKEET_DESCRIPTOR_NATIVE find_pe_section_native(const std::byte* nt_header,
                                                                std::uint32_t hash) {
    return find_pe_section(nt_header, hash); //433A1085 ECX/EDX,RET0
}
RttiTypeDescriptor* SKEET_DESCRIPTOR_NATIVE find_rtti_type_native(std::byte* module,
                                                                  std::uint32_t hash) {
    return find_rtti_type(module, hash, active_runtime->images()); //433B305B ECX/EDX,RET0
}
void** SKEET_DESCRIPTOR_NATIVE find_rtti_vtable_native(std::byte* module, RttiTypeDescriptor* type,
                                                       std::uint32_t base_hash) {
    return find_rtti_vtable(module, type, base_hash,
                            active_runtime->images()); //4332CE3A ECX/EDX,RET4
}
void** SKEET_DESCRIPTOR_NATIVE find_rtti_vtable_at_offset_native(std::byte* module,
                                                                 RttiTypeDescriptor* type,
                                                                 std::uint32_t offset) {
    return find_rtti_vtable_at_offset(module, type, offset,
                                      active_runtime->images()); //433EC4BD ECX/EDX,RET4
}
#undef SKEET_DESCRIPTOR_NATIVE
}
