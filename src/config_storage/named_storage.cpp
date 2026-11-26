#include "skeet/config_storage/named_storage.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

namespace skeet {
namespace {
std::uint32_t read32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8);
    return result;
}
void append32(ConfigBytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(std::byte((value >> (i * 8)) & 255));
}
bool read_block(std::span<const std::byte> bytes, std::size_t& cursor,
                std::span<const std::byte>& block) {
    if (cursor >= bytes.size() || bytes[cursor++] != std::byte{1})
        return false;
    if (bytes.size() - cursor < 4)
        return false;
    const auto size = read32(bytes, cursor);
    cursor += 4;
    if (size > bytes.size() - cursor)
        return false;
    block = bytes.subspan(cursor, size);
    cursor += size;
    return true;
}
bool read_record(std::span<const std::byte> bytes, std::size_t& cursor, NamedConfigRecord& record) {
    std::span<const std::byte> name, data;
    if (!read_block(bytes, cursor, name) || !read_block(bytes, cursor, data))
        return false;
    if (bytes.size() - cursor < 2 || bytes[cursor] != std::byte{3})
        return false;
    record.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
    record.data.assign(data.begin(), data.end());
    record.key = std::to_integer<std::uint8_t>(bytes[cursor + 1]);
    cursor += 2;
    return true;
}
void append_block(ConfigBytes& bytes, std::span<const std::byte> data) {
    if (data.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Native named configuration block exceeds uint32");
    bytes.push_back(std::byte{1});
    append32(bytes, static_cast<std::uint32_t>(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
}
}

NamedConfigListing parse_named_config_container(std::span<const std::byte> bytes) {
    NamedConfigListing result;
    if (bytes.empty() || bytes.front() != std::byte{0xAF}) {
        result.malformed = true;
        return result;
    }
    std::size_t cursor = 1;
    while (cursor < bytes.size()) {
        NamedConfigRecord record;
        if (!read_record(bytes, cursor, record)) {
            result.malformed = true;
            break;
        }
        result.records.push_back(std::move(record));
    }
    return result;
}

// 433E00BE iterates the native map in bytewise ascending name order. Empty
// names and empty values remain valid map nodes but are omitted from the file.
ConfigBytes serialize_named_config_container(std::span<const NamedConfigRecord> records) {
    std::map<std::string, const NamedConfigRecord*, std::less<>> ordered;
    for (const auto& record : records)
        ordered.try_emplace(record.name, &record);
    ConfigBytes output{std::byte{0xAF}};
    for (const auto& [name, record] : ordered) {
        if (name.empty() || record->data.empty())
            continue;
        append_block(output, std::as_bytes(std::span(name.data(), name.size())));
        append_block(output, record->data);
        output.push_back(std::byte{3});
        output.push_back(std::byte(record->key));
    }
    return output;
}

std::vector<NamedConfigRecord> NamedConfigStore::list() {
    const auto bytes = backend_.read_container();
    if (!bytes)
        return {};
    auto listing = parse_named_config_container(*bytes);
    if (listing.malformed)
        backend_.remove_container();
    return std::move(listing.records);
}

ConfigBytes NamedConfigStore::load(std::string_view name) {
    const auto bytes = backend_.read_container();
    if (!bytes)
        return {};
    if (bytes->empty() || bytes->front() != std::byte{0xAF}) {
        backend_.remove_container();
        return {};
    }
    std::size_t cursor = 1;
    while (cursor < bytes->size()) {
        NamedConfigRecord record;
        if (!read_record(*bytes, cursor, record)) {
            backend_.remove_container();
            return {};
        }
        if (record.name == name)
            return std::move(record.data);
    }
    return {};
}

void NamedConfigStore::save(std::string_view name, std::span<const std::byte> data,
                            std::uint8_t key) {
    name = name.substr(0, name.find('\0'));
    std::map<std::string, NamedConfigRecord, std::less<>> entries;
    for (auto& record : list())
        entries.try_emplace(record.name, std::move(record));
    if (data.empty()) {
        entries.erase(std::string(name));
    } else {
        if (key != 0x1B) {
            for (const auto& [existing_name, record] : entries) {
                const std::string_view existing(existing_name);
                if (record.key == key && existing.substr(0, existing.find('\0')) != name) {
                    key = 0x1B;
                    break;
                }
            }
        }
        entries.insert_or_assign(
            std::string(name),
            NamedConfigRecord{std::string(name), ConfigBytes(data.begin(), data.end()), key});
    }
    if (entries.empty()) {
        backend_.remove_container();
        return;
    }
    std::vector<NamedConfigRecord> ordered;
    ordered.reserve(entries.size());
    for (auto& [_, record] : entries)
        ordered.push_back(std::move(record));
    backend_.write_container(serialize_named_config_container(ordered));
}

} // namespace skeet
