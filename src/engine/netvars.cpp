#include "skeet/engine/netvars.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace skeet::engine {

std::uint32_t maximum_property_offset(const RecvTable& table, std::uint32_t base,
                                      std::uint32_t minimum) {
    auto maximum = minimum;
    for (std::int32_t i = 0; i < table.property_count; ++i) {
        const auto& property = table.properties[i];
        const auto offset = base + static_cast<std::uint32_t>(property.offset);
        maximum = std::max(maximum, offset);
        if (property.type == recv_data_table)
            maximum = maximum_property_offset(*property.data_table, offset, maximum);
    }
    return maximum;
}

std::uint32_t netvar_name_hash(const char* name) noexcept {
    std::uint32_t hash = 0x811c9dc5;
    if (name)
        for (const auto* byte = reinterpret_cast<const unsigned char*>(name); *byte; ++byte)
            hash = (hash ^ *byte) * 0x1000193;
    return hash ^ 0x92ca5df1;
}

ClientClass* find_client_class(void* base_client, std::uint32_t hash) {
    // Native 0x433412C5 loads global 0x434683E0; +0x20 is the x86 byte slot.
    auto* node = invoke<ClientClass*>(base_client, 8);
    while (node) {
        if (netvar_name_hash(node->network_name) == hash)
            return node;
        node = node->next;
    }
    return nullptr;
}

RecvTable* find_receive_table(RecvTable& table, std::uint32_t hash) {
    if (netvar_name_hash(table.name) == hash)
        return &table;
    for (std::int32_t i = 0; i < table.property_count; ++i) {
        auto& property = table.properties[i];
        if (property.type == recv_data_table) {
            if (auto* found = find_receive_table(*property.data_table, hash))
                return found;
        }
    }
    return nullptr;
}

RecvProp* find_receive_property(RecvTable& table, std::uint32_t hash) {
    for (std::int32_t i = 0; i < table.property_count; ++i) {
        auto& property = table.properties[i];
        if (netvar_name_hash(property.name) == hash)
            return &property;
        if (property.type == recv_data_table) {
            if (auto* found = find_receive_property(*property.data_table, hash))
                return found;
        }
    }
    return nullptr;
}

std::uint32_t find_receive_offset(RecvTable& table, std::uint32_t hash, std::uint32_t base) {
    for (std::int32_t i = 0; i < table.property_count; ++i) {
        auto& property = table.properties[i];
        if (std::strncmp(property.name, "base", 4) == 0)
            continue;
        const auto offset = base + static_cast<std::uint32_t>(property.offset);
        if (netvar_name_hash(property.name) == hash)
            return offset;
        if (property.type == recv_data_table && property.data_table->property_count > 0) {
            const auto found = find_receive_offset(*property.data_table, hash, offset);
            if (found != std::numeric_limits<std::uint32_t>::max())
                return found;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

RecvProp* find_receive_property(void* base_client, std::uint32_t class_hash,
                                std::uint32_t table_hash, std::uint32_t property_hash) {
    auto* node = find_client_class(base_client, class_hash);
    if (!node)
        return nullptr;
    auto* table = find_receive_table(*node->table, table_hash);
    return table ? find_receive_property(*table, property_hash) : nullptr;
}

void bind_receive_offset(void* base_client, const OffsetBinding& binding, std::uint32_t*& cursor) {
    auto* node = find_client_class(base_client, binding.class_hash);
    if (!node)
        return;
    const auto offset = find_receive_offset(*node->table, binding.property_hash);
    *binding.pointer_output = cursor;
    *binding.xor_key ^= *cursor ^ (offset + binding.addend);
    ++cursor;
}

std::uint32_t decode_receive_offset(const std::uint32_t* pointer, std::uint32_t xor_key,
                                    std::uint32_t consumer_key) noexcept {
    return *pointer ^ xor_key ^ consumer_key;
}

} // namespace skeet::engine
