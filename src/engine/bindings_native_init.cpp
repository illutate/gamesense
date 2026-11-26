#include "skeet/engine/bindings.hpp"

namespace skeet::engine {
namespace {
void initialize_receive_binding(void* base_client, InitializationMetadata& metadata,
                                std::uint32_t address, std::uint32_t*& cursor, bool with_addend) {
    OffsetBinding binding;
    binding.class_hash = static_cast<std::uint32_t>(metadata.operand(address));
    binding.property_hash = static_cast<std::uint32_t>(metadata.operand(address + 4));
    if (with_addend)
        binding.addend = static_cast<std::uint32_t>(metadata.operand(address + 16));
    binding.xor_key = reinterpret_cast<std::uint32_t*>(metadata.operand(address + 8));
    binding.pointer_output = reinterpret_cast<std::uint32_t**>(metadata.operand(address + 12));
    bind_receive_offset(base_client, binding, cursor);
}
}

void initialize_native_netvars(void* base_client, std::byte* client_module,
                               InitializationMetadata& metadata, std::uint32_t*& cursor,
                               DiscoveredDataMaps& maps) {
    for (std::uint32_t descriptor = 0x1678; descriptor != 0x24e8; descriptor += 0x10)
        initialize_receive_binding(base_client, metadata, descriptor, cursor, false);
    for (std::uint32_t descriptor = 0x1390; descriptor != 0x1674; descriptor += 0x14)
        initialize_receive_binding(base_client, metadata, descriptor, cursor, true);
    maps.c69c5c29 = find_data_map(client_module, 0xc69c5c29);
    maps.a9cd7264 = find_data_map(client_module, 0xa9cd7264);
    for (std::uint32_t descriptor = 0x1278; descriptor != 0x1390; descriptor += 0x14) {
        DataMapOffsetBinding binding;
        binding.map = *reinterpret_cast<DataMap**>(metadata.operand(descriptor));
        binding.table_hash = static_cast<std::uint32_t>(metadata.operand(descriptor + 4));
        binding.property_hash = static_cast<std::uint32_t>(metadata.operand(descriptor + 8));
        binding.xor_key = reinterpret_cast<std::uint32_t*>(metadata.operand(descriptor + 12));
        binding.pointer_output =
            reinterpret_cast<std::uint32_t**>(metadata.operand(descriptor + 16));
        bind_data_map_offset(binding, cursor);
    }
}
} // namespace skeet::engine
