#include "skeet/skins/native_schema.hpp"
#include "skeet/config_storage/owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/events/game_events.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>

namespace skeet::skins {
bool NativeSchemaEngine::acquire_schema() {
    if (!bindings_.item_system && bindings_.acquire_item_system)
        bindings_.item_system = bindings_.acquire_item_system();
    schema_ = bindings_.item_system ? engine::invoke<void*>(bindings_.item_system, 0) : nullptr;
    return schema_ != nullptr;
}
std::int32_t NativeSchemaEngine::definition_count() {
    return engine::invoke<std::int32_t>(schema_, 3); //433CD70E, vtable+0C
}
std::optional<SchemaItem> NativeSchemaEngine::definition(std::uint32_t id) {
    auto* item = engine::invoke<void*>(schema_, 4, id); //433CD70E, vtable+10
    if (!item)
        return std::nullopt;
    SchemaItem result;
    result.type_token = engine::invoke<const char*>(item, 3);
    if (result.type_token) {
        const auto type = events::event_hash(result.type_token);
        // Preserve the actual branch-specific virtual getter order. Agent
        // definitions never query the glove-only world model method.
        if (type == 0x2B77F7D8) {
            result.view_model = engine::invoke<const char*>(item, 6);
            result.base_name_token = engine::invoke<const char*>(item, 2);
        } else if (type == 0x8E483246) {
            result.world_model = engine::invoke<const char*>(item, 7);
            result.view_model = engine::invoke<const char*>(item, 6);
            result.base_name_token = engine::invoke<const char*>(item, 2);
        }
    }
    return result;
}
const char* NativeSchemaEngine::paintkit_token(std::uint32_t id) {
    if (!bindings_.paintkit_definition)
        return nullptr;
    const auto* definition = bindings_.paintkit_definition(schema_, id);
    if (!definition)
        return nullptr;
    const char* token;
    std::memcpy(&token, static_cast<const std::byte*>(definition) + 0x24, sizeof(token));
    return token;
}
std::optional<std::u16string> NativeSchemaEngine::localize(const char* token) {
    //43397087/433E533F ultimately call this actual localization vtable entry.
    const auto* text = engine::invoke<const char16_t*>(bindings_.localization, 11, token);
    if (!text)
        return std::nullopt;
    return std::u16string(text);
}
std::u16string NativeSchemaFiles::current_directory() {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    std::array<wchar_t, 260> directory;
    const auto length = GetCurrentDirectoryW(DWORD(directory.size()), directory.data());
    if (!length)
        return {};
    if (length >= directory.size())
        throw std::length_error(
            "Native schema current directory exceeds its 260-character storage");
    return {reinterpret_cast<const char16_t*>(directory.data()), length};
}
std::optional<std::string> NativeSchemaFiles::read_file(std::u16string_view path) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::u16string terminated(path);
    UNICODE_STRING name{};
    if (!RtlDosPathNameToNtPathName_U(reinterpret_cast<const wchar_t*>(terminated.c_str()), &name,
                                      nullptr, nullptr))
        return std::nullopt;
    struct Release {
        UNICODE_STRING& name;
        ~Release() {
            RtlFreeUnicodeString(&name);
        }
    } release{name};
    const auto bytes = read_native_file(
        {name.Length, name.MaximumLength, reinterpret_cast<char16_t*>(name.Buffer)});
    if (!bytes)
        return std::nullopt;
    if (bytes->empty())
        return std::string{};
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}
} //namespace skeet::skins
