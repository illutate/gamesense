#include "skeet/engine/native_service_table.hpp"
#include "skeet/engine/netvars.hpp"
#include <bit>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* address) noexcept {
    T value;
    std::memcpy(&value, address, sizeof value);
    return value;
}
int compare_service_rva(const void* first, const void* second) noexcept { //433F1E1B
    const auto& a = *static_cast<const NativeServiceRecord*>(first);
    const auto& b = *static_cast<const NativeServiceRecord*>(second);
    return std::bit_cast<std::int32_t>(a.function_rva - b.function_rva);
}
}

void initialize_native_service_table(NativeServiceTable& table, const LoaderData& loader,
                                     NativeImageEnvironment images,
                                     ImageDirectoryEntry image_directory_entry) {
    table.count = 0;
    const auto* entry = reinterpret_cast<const LoaderModule*>(loader.modules.next->next);
    auto* const module = static_cast<std::byte*>(entry->base);
    if (!module || !images.image_nt_header(module))
        return;
    std::uint32_t directory_size;
    auto* const directory = image_directory_entry(module, 1, 0, &directory_size);
    if (!directory)
        return;
    const auto* functions = module + read<std::uint32_t>(directory + 0x1C);
    const auto* ordinals = module + read<std::uint32_t>(directory + 0x24);
    const auto* names = module + read<std::uint32_t>(directory + 0x20);
    const auto count = read<std::uint32_t>(directory + 0x18);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* name =
            reinterpret_cast<const char*>(module + read<std::uint32_t>(names + index * 4));
        if (name[0] != 'N' || name[1] != 't')
            continue;
        // The native caller reserves exactly1024 records and supplies ntdll's
        // valid export directory. Reject a source-side capacity violation.
        if (table.count == table.records.size())
            throw std::length_error("Native service export table exceeds its 1024-record capacity");
        auto& record = table.records[table.count++];
        record.name_hash = netvar_name_hash(name); //43359C70, case-sensitive FNV1a and salt
        const auto ordinal = read<std::uint16_t>(ordinals + index * 2);
        record.function_rva = read<std::uint32_t>(functions + ordinal * 4);
        record.service_number = 0xFFFFFFFFu;
        const auto* code = module + record.function_rva;
        if (code[0] == std::byte{0xB8})
            record.service_number = read<std::uint32_t>(code + 1);
    }
    std::qsort(table.records.data(), table.count, sizeof(NativeServiceRecord), compare_service_rva);
    std::uint32_t previous = 0;
    for (std::uint32_t index = 0; index < table.count; ++index) {
        auto& record = table.records[index];
        if (record.service_number != 0xFFFFFFFFu)
            previous = record.service_number & 0xFFFu;
        else
            record.service_number = ++previous;
    }
}
std::uint32_t resolve_native_service_number(const NativeServiceTable& table, std::uint32_t hash,
                                            NativeServiceTerminateProcess terminate_process) {
    for (std::uint32_t index = 0; index < table.count; ++index) {
        const auto& record = table.records[index];
        if (record.name_hash != hash)
            continue;
        if (record.service_number != 0xFFFFFFFFu)
            return static_cast<std::uint16_t>(record.service_number);
        break;
    }
    terminate_process(reinterpret_cast<void*>(~std::uintptr_t{0}), -10);
    return 0xFFFFFFFFu;
}
} // namespace skeet::engine
