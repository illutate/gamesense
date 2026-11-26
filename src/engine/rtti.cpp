#include "skeet/engine/rtti.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}
std::uint32_t section_hash(const char* name) { // 4334DEFF
    std::uint32_t result = 0x811c9dc5;
    for (auto value = static_cast<std::uint8_t>(*name); value;
         value = static_cast<std::uint8_t>(*++name)) {
        if (static_cast<std::uint8_t>(value - 'A') <= 'Z' - 'A')
            value += 'a' - 'A';
        result = (result ^ value) * 0x1000193;
    }
    return result ^ 0x92ca5df1;
}
struct SectionRange {
    std::byte* begin;
    std::byte* end;
};
SectionRange rdata(std::byte* module, NativeImageEnvironment images) {
    const auto* section = find_pe_section(images.image_nt_header(module), 0x33d66818);
    if (!section)
        return {};
    auto* begin = module + read<std::uint32_t>(section + 0xc);
    return {begin, begin + read<std::uint32_t>(section + 8)};
}
bool within(const void* pointer, SectionRange section) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= reinterpret_cast<std::uintptr_t>(section.begin) &&
           address < reinterpret_cast<std::uintptr_t>(section.end);
}
void** table_after_locator(SectionRange section, const std::byte* locator) {
    for (auto* position = section.begin; position < section.end; position += 4)
        if (read<const std::byte*>(position) == locator)
            return reinterpret_cast<void**>(position + sizeof(void*));
    return nullptr;
}
}

const std::byte* find_pe_section(const std::byte* nt_header, std::uint32_t name_hash) { // 433A1085
    if (!nt_header)
        return nullptr;
    const auto count = read<std::uint16_t>(nt_header + 6);
    auto* section = nt_header + 0x18 + read<std::uint16_t>(nt_header + 0x14);
    for (std::uint16_t i = 0; i < count; ++i, section += 0x28) {
        std::array<char, 9> name{};
        std::memcpy(name.data(), section, 8);
        if (section_hash(name.data()) == name_hash)
            return section;
    }
    return nullptr;
}

RttiTypeDescriptor* find_rtti_type(std::byte* module, std::uint32_t type_hash,
                                   NativeImageEnvironment images) { // 433B305B
    const auto* section = find_pe_section(images.image_nt_header(module), 0xe2d1c070);
    if (!section)
        return nullptr;
    auto* begin = module + read<std::uint32_t>(section + 0xc);
    auto* const end = begin + read<std::uint32_t>(section + 8) - 10;
    for (auto* position = begin; position < end; position += 4) {
        if (read<std::uint32_t>(position) == 0x56413f2e &&
            netvar_name_hash(reinterpret_cast<const char*>(position)) == type_hash)
            return reinterpret_cast<RttiTypeDescriptor*>(position -
                                                         offsetof(RttiTypeDescriptor, name));
    }
    return nullptr;
}

void** find_rtti_vtable(std::byte* module, RttiTypeDescriptor* type, std::uint32_t base_hash,
                        NativeImageEnvironment images) { // 4332CE3A
    const auto section = rdata(module, images);
    if (!section.begin)
        return nullptr;
    std::array<const std::byte*, 32> candidates;
    std::uint32_t candidate_count = 0;
    auto desired_offset = base_hash ? 0xffffffffu : 0u;
    const std::byte* selected = nullptr;
    for (auto* position = section.begin; position < section.end; position += 4) {
        if (read<RttiTypeDescriptor*>(position) != type)
            continue;
        const auto* locator = position - offsetof(RttiObjectLocator, type);
        const auto* hierarchy =
            read<RttiClassHierarchy*>(locator + offsetof(RttiObjectLocator, hierarchy));
        if (within(hierarchy, section)) {
            if (desired_offset == 0xffffffffu) {
                if (candidate_count < candidates.size())
                    candidates[candidate_count++] = locator;
            } else if (read<std::uint32_t>(locator + offsetof(RttiObjectLocator, offset)) ==
                       desired_offset) {
                selected = locator;
                break;
            }
            continue;
        }
        // The same type pointer can belong to a BaseClassDescriptor. Native
        // follows that record's hierarchy to obtain the requested base mdisp.
        hierarchy = read<RttiClassHierarchy*>(position + offsetof(RttiBaseClass, hierarchy));
        if (!hierarchy)
            continue;
        const auto* hierarchy_bytes = reinterpret_cast<const std::byte*>(hierarchy);
        auto** bases = read<RttiBaseClass**>(hierarchy_bytes + offsetof(RttiClassHierarchy, bases));
        if (!bases)
            continue;
        const auto count =
            read<std::uint32_t>(hierarchy_bytes + offsetof(RttiClassHierarchy, count));
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto* base = bases[i];
            if (!base)
                continue;
            const auto* bytes = reinterpret_cast<const std::byte*>(base);
            const auto* base_type =
                read<RttiTypeDescriptor*>(bytes + offsetof(RttiBaseClass, type));
            if (base_type && netvar_name_hash(reinterpret_cast<const char*>(base_type) +
                                              offsetof(RttiTypeDescriptor, name)) == base_hash) {
                desired_offset =
                    read<std::uint32_t>(bytes + offsetof(RttiBaseClass, member_displacement));
                break;
            }
        }
        if (desired_offset == 0xffffffffu)
            continue;
        for (std::uint32_t i = 0; i < candidate_count; ++i) {
            if (read<std::uint32_t>(candidates[i] + offsetof(RttiObjectLocator, offset)) ==
                desired_offset) {
                selected = candidates[i];
                break;
            }
        }
        if (selected)
            break;
    }
    return selected ? table_after_locator(section, selected) : nullptr;
}

void** find_rtti_vtable_at_offset(std::byte* module, RttiTypeDescriptor* type, std::uint32_t offset,
                                  NativeImageEnvironment images) { // 433EC4BD
    const auto section = rdata(module, images);
    if (!section.begin)
        return nullptr;
    for (auto* position = section.begin; position < section.end; position += 4) {
        if (read<RttiTypeDescriptor*>(position) != type)
            continue;
        const auto* locator = position - offsetof(RttiObjectLocator, type);
        if (read<std::uint32_t>(locator + offsetof(RttiObjectLocator, constructor_displacement)) ==
                0 &&
            read<std::uint32_t>(locator + offsetof(RttiObjectLocator, offset)) == offset)
            return table_after_locator(section, locator);
    }
    return nullptr;
}

void** resolve_rtti_vtable(std::byte* module, std::uint32_t type_hash, std::uint32_t base_hash,
                           NativeImageEnvironment images) {
    return find_rtti_vtable(module, find_rtti_type(module, type_hash, images), base_hash, images);
}
}
