#include "skeet/esp/flag_table_initializer.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/engine/preserved_initialization.hpp"
#include "flag_table_detail.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::esp {
namespace {
FlagTableInitialization* active_initialization = nullptr;
std::uint32_t address(const void* pointer) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}
char16_t* pointer(std::uint32_t value) noexcept {
    return reinterpret_cast<char16_t*>(std::uintptr_t(value));
}
void* allocate_text(const engine::JobMemory& memory, std::uint32_t bytes) { //43387448
    if (!bytes)
        return nullptr;
    auto* const raw = memory.allocate(memory.process_heap, 8, bytes + 0x20u);
    if (!raw)
        return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (base + 15u) & ~std::uintptr_t(15);
    const auto displacement = static_cast<std::uint32_t>(header - base);
    const auto allocation_size = bytes + 0x20u;
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free_text(const engine::JobMemory& memory, std::uint32_t value) { //433837D0
    if (!value)
        return;
    auto* const bytes = reinterpret_cast<std::byte*>(std::uintptr_t(value));
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void finalize_flags() {
    active_initialization->flags.finalize();
}
void initialize_flags(void* owner) {
    auto& context = *static_cast<FlagTableInitialization*>(owner);
    context.flags.initialize(); //43407D58 CALL43485097 precedes registration
    (void)context.callbacks.append(context.source_finalizer(0x43401BC2u));
}
}

FlagTable::FlagTable(engine::PreservedInitialization& initialization, engine::JobMemory memory)
    : FlagTable(initialization.image_owner(), memory) {}
FlagTable::FlagTable(std::shared_ptr<engine::NativeDataImage> image, engine::JobMemory memory)
    : image_(image), memory_(memory),
      rows_(engine::require_native_data_image(image_).native_cell<std::array<NativeFlagRow, 18>>(
          0x43479120u)),
      shared_empty_(image_->native_cell<char16_t>(0x434759ACu)) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Native ESP flag strings require their process heap");
}

void FlagTable::construct_text(NativeFlagRow& row, const char16_t* source) {
    //433D2786 clears only the pointer triple, then scans the terminated source.
    row.first = 0;
    row.last = 0;
    row.limit = 0;
    std::uint32_t length = 0;
    while (source[length])
        ++length;
    const auto capacity = length + 1u;
    std::uint32_t first, limit;
    if (capacity < 2) { //4339E00C shared two-byte empty allocation
        first = address(&shared_empty_);
        limit = first + 2u;
    } else {
        first = address(allocate_text(memory_, capacity * 2u));
        limit = first + capacity * 2u;
    }
    row.first = first;
    row.last = first;
    row.limit = limit;
    //43342C16 caches the newly published first pointer across memmove. Its
    // last-pointer write and terminator use that same destination afterwards.
    const auto destination = row.first;
    const auto copied = length * 2u;
    std::memmove(pointer(destination), source, copied);
    row.last = destination + copied;
    *pointer(destination + copied) = 0;
}

void FlagTable::initialize() { //43485097..434856C5
    for (std::size_t index = 0; index < 15; ++index) {
        const auto initial = detail::initial_flag_entry(index);
        auto& row = rows_[index];
        row.mask = initial.mask;
        row.color = initial.color;
        construct_text(row, initial.text.data());
    }
    //43485691 clears all72 bytes before publishing the three empty triples.
    std::memset(rows_.data() + 15, 0, 3 * sizeof(NativeFlagRow));
    const auto empty = address(&shared_empty_);
    for (std::size_t index = 15; index < 18; ++index) {
        auto& row = rows_[index];
        row.first = empty;
        row.last = empty;
        row.limit = empty + 2u;
    }
}

void FlagTable::finalize() { //43401BC2 /433C9151 /433823B5
    for (std::size_t index = 18; index; --index) {
        const auto& row = rows_[index - 1];
        const auto capacity = (row.limit - row.first) & 0xFFFFFFFEu;
        if (static_cast<std::int32_t>(capacity) > 2 && row.first)
            free_text(memory_, row.first);
    }
    //The terminal native destructor leaves every row word unchanged.
}
std::u16string_view FlagTable::text(std::size_t index) const noexcept {
    const auto& row = rows_[index];
    return {pointer(row.first), std::uint32_t(row.last - row.first) / 2u};
}

void (*FlagTableInitialization::source_finalizer(std::uint32_t native_address))() {
    if (native_address != 0x43401BC2u)
        throw std::out_of_range("Finalizer is not the recovered ESP flag-table entry");
    if (active_initialization && active_initialization != this)
        throw std::logic_error("ESP flag-table exit callback requires one stable source binding");
    active_initialization = this;
    return finalize_flags;
}
void bind_flag_table_initializer(client::NativeStartupSources& sources,
                                 FlagTableInitialization& context) {
    sources.bind_initializer({0x43407D58u, &context, initialize_flags});
}
} //namespace skeet::esp
