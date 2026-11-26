#include "skeet/config_storage/native_blob_owner.hpp"
#include "skeet/config_storage/storage.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet {
namespace {
struct NativeStringDescriptor {
    ConfigDescriptor header;
    NativeBlobCells cells;
    std::uint32_t trailing;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(NativeStringDescriptor) == 0x20);
#endif
void release_descriptor(ConfigDescriptor& descriptor, const engine::JobMemory& memory) {
    auto* bytes = reinterpret_cast<std::byte*>(&descriptor);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement); //433837D0
}
NativeStringDescriptor* allocate_descriptor(const engine::JobMemory& memory) {
    constexpr std::uint32_t size = 0x20; //433CBD22 PUSH20h;433CBD24 POPECX
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20); //43387448
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    constexpr std::uint32_t allocated = size + 0x20;
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocated, 4);
    return reinterpret_cast<NativeStringDescriptor*>(header + 16);
}
}
NativeConfigBlobOwner::NativeConfigBlobOwner(ConfigRegistries& registry, engine::JobMemory memory,
                                             ConfigDescriptor& header, NativeBlobCells& cells,
                                             std::byte& empty)
    : registry_(registry), memory_(memory), header_(header), bytes_(cells, memory, empty) {
    configuration_.value.bind_native(bytes_);
    registry_.bind_blob(header_, configuration_);
}
std::unique_ptr<NativeConfigBlobOwner> NativeConfigBlobOwner::create(ConfigRegistries& registry,
                                                                     engine::JobMemory memory,
                                                                     std::byte& empty,
                                                                     std::uint32_t hash) {
    if (sizeof(void*) != 4)
        throw std::logic_error(
            "Dynamic native configuration strings require their original x86 layout");
    if (!memory.allocate || !memory.free || !memory.process_heap)
        throw std::invalid_argument(
            "Dynamic native configuration strings require their actual process heap");
    auto* native = allocate_descriptor(memory);
    if (!native)
        return nullptr;
    std::unique_ptr<NativeConfigBlobOwner> owner;
    try {
        owner.reset(
            new NativeConfigBlobOwner(registry, memory, native->header, native->cells, empty));
    } catch (...) {
        release_descriptor(native->header, memory);
        throw;
    }
    initialize_blob_config(registry.descriptors, owner->configuration_, hash);
    return owner;
}
NativeConfigBlobOwner::~NativeConfigBlobOwner() {
    //433ECDEE unlinks the actual header before this release. It calls only
    //433837D0 here; it does not dispatch a tag12 buffer destructor.
    registry_.unbind_blob(configuration_);
    release_descriptor(header_, memory_);
}
} // namespace skeet
