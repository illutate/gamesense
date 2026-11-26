#include "skeet/rage/acquisition.hpp"
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace skeet::rage {
AcquisitionEvaluations::~AcquisitionEvaluations() {
    release();
}
void AcquisitionEvaluations::allocate(engine::JobMemory memory) { //434805C0..434805E3
    if (data_)
        throw std::logic_error("RAGE evaluation storage is already allocated");
    memory_ = memory;
    constexpr auto size = 80u * sizeof(AcquisitionEvaluation);
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    const auto allocation_size = static_cast<std::uint32_t>(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    data_ = reinterpret_cast<AcquisitionEvaluation*>(header + 16);
    for (std::size_t index = 0; index < 80; ++index)
        ::new (static_cast<void*>(data_ + index)) AcquisitionEvaluation;
}
void AcquisitionEvaluations::release() noexcept { //43387719
    if (!data_)
        return;
    for (std::size_t index = 0; index < 80; ++index)
        std::destroy_at(data_ + index);
    auto* bytes = reinterpret_cast<std::byte*>(data_);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory_.free(memory_.process_heap, 0, bytes - 16 - displacement);
    data_ = nullptr;
}
}
