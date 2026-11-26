#include "skeet/rage/spread_storage.hpp"
#include "skeet/engine/unload.hpp"
#include <cstring>

namespace skeet::rage {
namespace {
SpreadSeed* allocate_plane(const engine::JobMemory& memory) { //43387448(size1800)
    constexpr std::uint32_t allocation_size = 0x1820;
    auto* raw = memory.allocate(memory.process_heap, 8, allocation_size);
    if (!raw)
        return nullptr; //the actual allocation helper's null return
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - address);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<SpreadSeed*>(header + 16);
}
void release_plane(SpreadSeed* plane, const engine::JobMemory& bindings) {
    engine::UnloadSystemFunctions memory;
    memory.free_heap = bindings.free;
    memory.process_heap = bindings.process_heap;
    engine::free_native_allocation(plane, memory);
}
}
SpreadStorage::SpreadStorage(engine::PreservedInitialization& initialization)
    : SpreadStorage(initialization.image_owner()) {}
SpreadStorage::SpreadStorage(std::shared_ptr<engine::NativeDataImage> image)
    : image_(image),
      standard_(engine::require_native_data_image(image_).native_cell<SpreadSeed*>(0x43478C60u)),
      shotgun_patterns_(image_->native_cell<SpreadSeed*>(0x43478C64u)),
      tables_(standard_, shotgun_patterns_) {}
const SpreadSeed* const* SpreadStorage::initialize(const SpreadStorageBindings& bindings) {
    standard_ = allocate_plane(bindings.memory);
    auto* const second = allocate_plane(bindings.memory);
    // Native reloads the first global only after the second heap call. It
    // caches that pointer for the first loop, then reloads the second global
    // after all first-plane RNG/trigonometry calls have completed.
    auto* const first = standard_;
    shotgun_patterns_ = second;
    fill_spread_plane(std::span<SpreadSeed, 256>(first, 256), false, bindings.random,
                      bindings.trigonometry);
    auto* const patterns = shotgun_patterns_;
    fill_spread_plane(std::span<SpreadSeed, 256>(patterns, 256), true, bindings.random,
                      bindings.trigonometry);
    return &standard_;
}
void SpreadStorage::finalize(const SpreadStorageBindings& bindings) {
    if (shotgun_patterns_) {
        release_plane(shotgun_patterns_, bindings.memory);
        shotgun_patterns_ = nullptr;
    }
    if (standard_) {
        release_plane(standard_, bindings.memory);
        standard_ = nullptr;
    }
}
} // namespace skeet::rage
