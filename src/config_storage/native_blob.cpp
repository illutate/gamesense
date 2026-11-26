#include "skeet/config_storage/native_blob.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace skeet {
namespace {
std::byte* pointer(std::uint32_t address) noexcept {
    return reinterpret_cast<std::byte*>(std::uintptr_t(address));
}
std::uint32_t address(const std::byte* value) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
}
}
NativeBlobBytes::NativeBlobBytes(NativeBlobCells& native, engine::JobMemory memory,
                                 std::byte& shared_empty)
    : cells(native), memory_(memory), shared_empty_(shared_empty) {
    if (sizeof(void*) != 4)
        throw std::logic_error(
            "Native configuration blobs require the original x86 pointer layout");
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Native configuration blobs require their process heap");
}
void NativeBlobBytes::reset_empty() noexcept {
    const auto empty_address = address(&shared_empty_);
    cells.limit = empty_address + 1;
    cells.first = empty_address;
    cells.last = empty_address;
}
std::byte* NativeBlobBytes::data() noexcept {
    return pointer(cells.first);
}
const std::byte* NativeBlobBytes::data() const noexcept {
    return pointer(cells.first);
}
std::byte* NativeBlobBytes::end() noexcept {
    return pointer(cells.last);
}
const std::byte* NativeBlobBytes::end() const noexcept {
    return pointer(cells.last);
}
std::size_t NativeBlobBytes::size() const noexcept {
    return std::uint32_t(cells.last - cells.first);
}
std::size_t NativeBlobBytes::capacity() const noexcept {
    return std::uint32_t(cells.limit - cells.first);
}
std::byte* NativeBlobBytes::allocate(std::size_t length) { //43387448
    auto* raw = memory_.allocate(memory_.process_heap, 8, length + 0x20);
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    const auto allocation_size = static_cast<std::uint32_t>(length + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<std::byte*>(header + 16);
}
void NativeBlobBytes::free(std::byte* value) { //433837D0
    if (!value)
        return;
    std::uint32_t displacement;
    std::memcpy(&displacement, value - 8, 4);
    memory_.free(memory_.process_heap, 0, value - 16 - displacement);
}
void NativeBlobBytes::terminal_release() { //4336B1E9
    if (static_cast<std::int32_t>(cells.limit - cells.first) > 1 && cells.first)
        free(data());
}
void NativeBlobBytes::append(std::span<const std::byte> source) { //433F4815
    if (source.empty())
        return;
    const auto previous_size = size();
    const auto previous_capacity = capacity() - 1;
    const auto required = previous_size + source.size();
    if (previous_capacity < required) {
        auto next = previous_capacity > 8 ? previous_capacity * 2 : std::size_t(8);
        next = std::max(next, required);
        auto* replacement = allocate(next + 1);
        if (previous_size)
            std::memmove(replacement, data(), previous_size);
        std::memmove(replacement + previous_size, source.data(), source.size());
        replacement[required] = std::byte{};
        terminal_release();
        cells.first = address(replacement);
        cells.last = address(replacement + required);
        cells.limit = address(replacement + next + 1);
    } else {
        // The first incoming byte is copied last, matching the overlapping
        // source case in the native in-capacity append path.
        auto* last = end();
        if (source.size() > 1)
            std::memmove(last + 1, source.data() + 1, source.size() - 1);
        last[source.size()] = std::byte{};
        *last = *source.data();
        cells.last += static_cast<std::uint32_t>(source.size());
    }
}
void NativeBlobBytes::assign(std::span<const std::byte> source) { //434015F3
    const auto previous_size = size();
    if (previous_size < source.size()) {
        if (previous_size)
            std::memmove(data(), source.data(), previous_size);
        append(source.subspan(previous_size));
    } else {
        if (!source.empty())
            std::memmove(data(), source.data(), source.size());
        if (source.size() != previous_size) { //43327B3E copies the existing terminator
            std::memmove(data() + source.size(), end(), 1);
            cells.last = cells.first + static_cast<std::uint32_t>(source.size());
        }
    }
}
void NativeBlobBytes::clear() noexcept {
    if (cells.first != cells.last) {
        std::memmove(data(), end(), 1);
        cells.last = cells.first;
    }
}
void NativeBlobBytes::resize(std::size_t length) {
    const auto previous_size = size();
    if (length <= previous_size) {
        if (length != previous_size) {
            std::memmove(data() + length, end(), 1);
            cells.last = cells.first + static_cast<std::uint32_t>(length);
        }
        return;
    }
    // Resize is the adapter's byte-value operation. RAGE's recovered paths
    // use assign, whose native prefix/capacity/aliasing order is above.
    const auto available = capacity() - 1;
    if (length > available) {
        auto next = available > 8 ? available * 2 : std::size_t(8);
        next = std::max(next, length);
        auto* replacement = allocate(next + 1);
        if (previous_size)
            std::memmove(replacement, data(), previous_size);
        std::memset(replacement + previous_size, 0, length - previous_size + 1);
        terminal_release();
        cells.first = address(replacement);
        cells.last = address(replacement + length);
        cells.limit = address(replacement + next + 1);
    } else {
        std::memset(end(), 0, length - previous_size + 1);
        cells.last = cells.first + static_cast<std::uint32_t>(length);
    }
}
}
