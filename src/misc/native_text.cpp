#include "skeet/misc/native_text.hpp"
#include <bit>
#include <cstring>
#include <intrin.h>

namespace skeet::misc {
namespace {
char empty_text[2]{}; //native434759AC/AD sentinel
void* allocate(engine::JobMemory memory, std::uint32_t length) { //43387448
    if (!length)
        return nullptr;
    const auto allocation_size = length + 0x20u;
    auto* raw = memory.allocate(memory.process_heap, 8, allocation_size);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free(engine::JobMemory memory, void* value) { //433837D0
    auto* bytes = static_cast<std::byte*>(value);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
void construct_native_text(NativeText& text, const char* value, engine::JobMemory memory) {
    text.begin = nullptr;
    text.end = nullptr;
    text.limit = nullptr;
    const char* end = value;
    while (*end)
        ++end;
    construct_native_text_range(text, value, end, memory);
}
void construct_native_text_range(NativeText& text, const char* first, const char* last,
                                 engine::JobMemory memory) {
    const auto length = std::uint32_t(reinterpret_cast<std::uintptr_t>(last) -
                                      reinterpret_cast<std::uintptr_t>(first));
    const auto capacity = length + 1u;
    auto* begin = capacity < 2 ? empty_text : static_cast<char*>(allocate(memory, capacity));
    text.begin = begin;
    text.end = begin;
    text.limit = capacity < 2 ? empty_text + 1 : begin + capacity;
    std::memmove(begin, first, length);
    text.end = begin + length;
    begin[length] = 0;
}
void destroy_native_text(NativeText& text, engine::JobMemory memory) {
    const auto capacity =
        std::bit_cast<std::int32_t>(std::uint32_t(reinterpret_cast<std::uintptr_t>(text.limit)) -
                                    std::uint32_t(reinterpret_cast<std::uintptr_t>(text.begin)));
    if (capacity >= 2 && text.begin)
        free(memory, text.begin);
}
void construct_native_encrypted_text(NativeEncryptedText& text, const char* value,
                                     std::uint32_t length, engine::JobMemory memory) {
    text.bytes = static_cast<char*>(allocate(memory, length + 1u));
    text.bytes[length] = 0;
    text.key = 0;
    std::uint8_t key;
    do
        key = std::uint8_t(0u - std::uint32_t(__rdtsc() & 0x7Fu));
    while (!key);
    text.key = key;
    for (std::uint32_t i = 0; i < length; ++i)
        text.bytes[i] = char(std::uint8_t(value[i]) ^ key);
}
void destroy_native_encrypted_text(NativeEncryptedText& text, engine::JobMemory memory) {
    if (!text.bytes)
        return;
    free(memory, text.bytes);
    text.bytes = nullptr;
}
void decode_native_text(const NativeEncryptedText& text, char* destination) noexcept {
    auto* encoded = reinterpret_cast<const std::uint8_t*>(text.bytes);
    while (*encoded)
        *destination++ = char(*encoded++ ^ text.key);
    *destination = 0;
}
} //namespace skeet::misc
