#include "skeet/engine/build_game_state_bindings.hpp"
#include "skeet/client/startup.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
struct SectionProfile {
    char name[8];
    std::uint32_t rva, size, flags;
};
struct BuildProfile {
    std::uint32_t nt_offset, timestamp, image_size, checksum;
    std::uint16_t section_count;
    std::array<SectionProfile, 3> sections;
};
constexpr BuildProfile client_build{0x150,
                                    0x65272025,
                                    0x5509000,
                                    0xFA635F,
                                    7,
                                    {{{".text", 0x1000, 0xB38409, 0x60000020},
                                      {".rdata", 0xB3A000, 0x26629C, 0x40000040},
                                      {".data", 0xDA1000, 0x460C3E0, 0xC0000040}}}};
constexpr BuildProfile engine_build{0x150,
                                    0x64EE7E86,
                                    0x39C0000,
                                    0x64174E,
                                    7,
                                    {{{".text", 0x1000, 0x451C32, 0x60000020},
                                      {".rdata", 0x453000, 0xD4766, 0x40000040},
                                      {".data", 0x528000, 0x3427638, 0xC0000040}}}};
struct Image {
    std::byte* base;
    const BuildProfile& build;
    bool contains(const void* pointer, std::size_t length, std::size_t section) const noexcept {
        const auto& region = build.sections[section];
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(base) + region.rva;
        return address >= begin && length <= region.size && address - begin <= region.size - length;
    }
    std::uint32_t rva(const void* pointer) const noexcept {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) -
                                          reinterpret_cast<std::uintptr_t>(base));
    }
};
Image checked_image(void* module, const BuildProfile& profile) {
    require(module != nullptr, "Game-state bindings require initialized client/engine modules");
    auto* const base = static_cast<std::byte*>(module);
    require(read<std::uint16_t>(base) == 0x5A4D &&
                read<std::uint32_t>(base + 0x3C) == profile.nt_offset,
            "Unsupported game-state DOS/NT-header layout");
    const auto* nt = base + profile.nt_offset;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == profile.section_count &&
                read<std::uint16_t>(nt + 0x14) == 0xE0 && read<std::uint16_t>(nt + 0x18) == 0x10B,
            "Game-state bindings require the verified PE32 layout");
    require(read<std::uint32_t>(nt + 8) == profile.timestamp &&
                read<std::uint32_t>(nt + 0x50) == profile.image_size &&
                read<std::uint32_t>(nt + 0x58) == profile.checksum,
            "Unsupported client/engine game-state build identity");
    std::array<bool, 3> found{};
    for (std::uint16_t index = 0; index < profile.section_count; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= profile.image_size && size <= profile.image_size - rva,
                "Game-state PE section exceeds its image");
        for (std::size_t wanted = 0; wanted < profile.sections.size(); ++wanted) {
            const auto& expected = profile.sections[wanted];
            if (std::memcmp(section, expected.name, 8))
                continue;
            require(!found[wanted] && rva == expected.rva && size == expected.size &&
                        read<std::uint32_t>(section + 36) == expected.flags,
                    "Unsupported game-state section fingerprint");
            found[wanted] = true;
        }
    }
    require(found[0] && found[1] && found[2], "Game-state PE section is missing");
    return {base, profile};
}
bool matches(const std::byte* code, std::span<const int> pattern) noexcept {
    for (std::size_t index = 0; index < pattern.size(); ++index)
        if (pattern[index] >= 0 && static_cast<unsigned char>(code[index]) != pattern[index])
            return false;
    return true;
}
void expect_code(const Image& image, const std::byte* code, std::initializer_list<int> pattern) {
    require(image.contains(code, pattern.size(), 0),
            "Game-state method is outside executable text");
    require(matches(code, {pattern.begin(), pattern.size()}),
            "Game-state method instruction fingerprint differs");
}
const std::byte* unique_code(const Image& image, std::initializer_list<int> pattern,
                             std::uint32_t expected_rva) {
    const auto& text = image.build.sections[0];
    const std::byte* found = nullptr;
    for (std::uint32_t offset = 0; offset <= text.size - pattern.size(); ++offset) {
        const auto* code = image.base + text.rva + offset;
        if (!matches(code, {pattern.begin(), pattern.size()}))
            continue;
        require(found == nullptr, "Game-state instruction anchor is ambiguous");
        found = code;
    }
    require(found && image.rva(found) == expected_rva,
            "Game-state instruction anchor is missing or unsupported");
    return found;
}
const std::byte* absolute(const Image& image, const std::byte* operand, std::size_t section,
                          std::size_t length) {
    const auto* pointer =
        reinterpret_cast<const std::byte*>(std::uintptr_t(read<std::uint32_t>(operand)));
    require(image.contains(pointer, length, section),
            "Game-state pointer has invalid module/section provenance");
    return pointer;
}
const std::byte* interface_table(const Image& image, void* object, std::size_t entries) {
    require(image.contains(object, 4, 2), "Game-state interface object is outside its module data");
    return absolute(image, static_cast<const std::byte*>(object), 1, entries * 4);
}
const std::byte* method(const Image& image, const std::byte* table, std::size_t slot,
                        std::uint32_t expected_rva) {
    const auto* function = absolute(image, table + slot * 4, 0, 1);
    require(image.rva(function) == expected_rva,
            "Game-state interface has an unsupported virtual method");
    return function;
}
void require_rtti(const Image& image, const std::byte* table, std::uint32_t object_offset,
                  const char* type_name) {
    require(image.contains(table - 4, 4, 1), "Game-state vtable has no RTTI prefix");
    const auto* locator = absolute(image, table - 4, 1, 20);
    require(read<std::uint32_t>(locator) == 0 &&
                read<std::uint32_t>(locator + 4) == object_offset &&
                read<std::uint32_t>(locator + 8) == 0,
            "Game-state RTTI locator has an unsupported object layout");
    const auto length = std::strlen(type_name) + 1;
    const auto* type = absolute(image, locator + 12, 2, 8 + length);
    absolute(image, locator + 16, 1, 16);
    require(std::memcmp(type + 8, type_name, length) == 0,
            "Game-state RTTI has an unexpected concrete type");
}
struct Acquired {
    std::byte* globals;
    void* input;
    void* client_state;
};
Acquired acquire(const Image& client, const Image& engine, void* base_client, void* engine_client) {
    const auto* client_table = interface_table(client, base_client, 17);
    const auto* initialize = method(client, client_table, 0, 0x25FC10);
    expect_code(client, initialize,
                {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0x0C, 0x01, 0,    0,
                 0x56, 0x83, 0xEC, 0x0C, 0xE8, -1,   -1,   -1,   -1,   0x8B, 0x45, 0x0C,
                 0x8D, 0x4D, 0x08, 0x83, 0xC4, 0x0C, 0xA3, -1,   -1,   -1,   -1});
    const auto* frame_stage = method(client, client_table, 11, 0x260740);
    expect_code(client, frame_stage, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0x83, 0xEC, 0x3C, 0xA1,
                                      -1,   -1,   -1,   -1,   0x56, 0xF3, 0x0F, 0x10, 0x40, 0x14});
    const auto* globals_slot = absolute(client, frame_stage + 0xA, 2, 4);
    require(read<std::uint32_t>(initialize + 0x1F) ==
                reinterpret_cast<std::uintptr_t>(globals_slot),
            "Client initialization and frame-stage globals operands disagree");
    auto* const globals = const_cast<std::byte*>(absolute(engine, globals_slot, 2, 0x40));

    const auto* input_call = method(client, client_table, 16, 0x260A60);
    expect_code(client, input_call, {0xA1, -1, -1, -1, -1, 0xB9, -1, -1, -1, -1, 0xFF, 0x60, 0x5C});
    const auto* input = absolute(client, input_call + 6, 2, 0xF8);
    require(read<std::uint32_t>(input_call + 1) == reinterpret_cast<std::uintptr_t>(input),
            "CInput virtual call uses different object and vtable operands");
    const auto* input_table = absolute(client, input, 1, 24 * 4);
    require_rtti(client, input_table, 0, ".?AVCCSInput@@");

    const auto* engine_table = interface_table(engine, engine_client, 13);
    const auto* local_player = method(engine, engine_table, 12, 0xB6800);
    expect_code(engine, local_player,
                {0xA1, -1,   -1, -1, -1, 0xB9, -1,   -1,   -1,   -1,   0xFF, 0x50, 0x14, 0x8B,
                 0x04, 0x85, -1, -1, -1, -1,   0x8B, 0x80, 0x80, 0x01, 0,    0,    0x40, 0xC3});
    const auto* get_state = unique_code(
        engine, {0x83, 0xF9, 0xFF, 0x75, 0x18, 0xA1, -1,   -1, -1,   -1,   0xB9, -1,   -1,   -1,
                 -1,   0xFF, 0x50, 0x14, 0x8B, 0x04, 0x85, -1, -1,   -1,   -1,   0x83, 0xC0, 0x08,
                 0xC3, 0x8B, 0x04, 0x8D, -1,   -1,   -1,   -1, 0x83, 0xC0, 0x08, 0xC3},
        0xE4490);
    const auto* manager = absolute(engine, get_state + 0xB, 2, 12);
    require(read<std::uint32_t>(get_state + 6) == reinterpret_cast<std::uintptr_t>(manager) &&
                read<std::uint32_t>(local_player + 1) ==
                    reinterpret_cast<std::uintptr_t>(manager) &&
                read<std::uint32_t>(local_player + 6) == reinterpret_cast<std::uintptr_t>(manager),
            "Engine client-state current-slot manager operands disagree");
    const auto* records = absolute(engine, get_state + 0x15, 2, 8);
    require(
        records == manager + 4 &&
            read<std::uint32_t>(get_state + 0x20) == reinterpret_cast<std::uintptr_t>(records) &&
            read<std::uint32_t>(local_player + 0x10) == reinterpret_cast<std::uintptr_t>(records),
        "Engine client-state current/explicit record operands disagree");
    const auto* manager_table = absolute(engine, manager, 1, 6 * 4);
    const auto* current_slot = absolute(engine, manager_table + 5 * 4, 0, 3);
    // This exact build's current-slot implementation always returns zero.
    // Requiring the real method body avoids inventing a split-screen policy.
    expect_code(engine, current_slot, {0x33, 0xC0, 0xC3});
    const auto record = read<std::uint32_t>(records);
    require(record != 0 && (record & 3) == 0 &&
                record <= std::numeric_limits<std::uint32_t>::max() - 12,
            "Engine current client-state record is not initialized");
    auto* const client_state = reinterpret_cast<std::byte*>(std::uintptr_t(record + 8));
    const auto* state_table = absolute(engine, client_state + 4, 1, 4);
    require_rtti(engine, state_table, 4, ".?AVCClientState@@");
    return {globals, const_cast<std::byte*>(input), client_state};
}
}

void initialize_build_game_state_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Game-state bindings require the original x86 ABI");
    require(data.size() >= NativeDataImage::image_size,
            "Game-state bindings require the complete canonical data image");
    const auto client = checked_image(data.native_cell<void*>(0x4346A55Cu), client_build);
    const auto engine = checked_image(data.native_cell<void*>(0x43468D20u), engine_build);
    const auto bindings = acquire(client, engine, data.native_cell<void*>(0x434683E0u),
                                  data.native_cell<void*>(0x4346A514u));
    data.native_cell<void*>(0x43467B04u) = bindings.globals;
    data.native_cell<void*>(0x4346A894u) = bindings.input;
    data.native_cell<void*>(0x43468E44u) = bindings.client_state;
    data.native_cell<float*>(0x43467EB8u) = reinterpret_cast<float*>(bindings.globals + 0x10);
    data.native_cell<std::int32_t*>(0x434678F8u) =
        reinterpret_cast<std::int32_t*>(bindings.globals + 4);
    data.native_cell<float*>(0x4346A338u) = reinterpret_cast<float*>(bindings.globals + 0x20);
}

void initialize_build_client_factories(NativeDataImage& data) {
    const auto client = checked_image(data.native_cell<void*>(0x4346A55Cu), client_build);
    const auto* glow =
        unique_code(client, {0xA1, -1,   -1, -1, -1, 0xA8, 1,  0x75, 0x4B, 0x0F, 0x57,
                             0xC0, 0xC7, 5,  -1, -1, -1,   -1, 0,    0,    0,    0,
                             0x0F, 0x11, 5,  -1, -1, -1,   -1, 0x83, 0xC8, 1},
                    0x2C8F50);
    expect_code(client, glow + 0x20,
                {0xC7, 5,  -1,   -1, -1,   -1,   0,  0,    0,  0,  0x0F, 0x28, 5,    -1,   -1,
                 -1,   -1, 0x68, -1, -1,   -1,   -1, 0xA3, -1, -1, -1,   -1,   0x0F, 0x11, 5,
                 -1,   -1, -1,   -1, 0xC7, 5,    -1, -1,   -1, -1, 0,    0,    0,    0,    0xE8,
                 -1,   -1, -1,   -1, 0x83, 0xC4, 4,  0xB8, -1, -1, -1,   -1,   0xC3});
    const auto* glow_guard = absolute(client, glow + 1, 2, 4);
    const auto* glow_object = absolute(client, glow + 0x19, 2, 0x2C);
    require(glow_object == glow_guard + 4 &&
                absolute(client, glow + 0xE, 2, 4) == glow_object + 0x20 &&
                absolute(client, glow + 0x22, 2, 4) == glow_object + 0x24 &&
                absolute(client, glow + 0x37, 2, 4) == glow_guard &&
                absolute(client, glow + 0x3E, 2, 16) == glow_object + 0x10 &&
                absolute(client, glow + 0x44, 2, 4) == glow_object + 0x28 &&
                absolute(client, glow + 0x55, 2, 0x2C) == glow_object,
            "Glow factory singleton initialization and return operands disagree");
    (void)absolute(client, glow + 0x2D, 1, 16);
    (void)absolute(client, glow + 0x32, 0, 1);

    const auto* items =
        unique_code(client, {0xA1, -1, -1,   -1,   -1,   0x85, 0xC0, 0x75, 0x53, 0xA1, -1,
                             -1,   -1, -1,   0x56, 0x68, 0xE8, 7,    0,    0,    0x8B, 8,
                             0x8B, 1,  0xFF, 0x50, 4,    0x8B, 0xF0, 0x85, 0xF6, 0x74, 0x2E},
                    0x712740);
    expect_code(client, items + 0x21,
                {0x68, 0xE8, 7,    0,    0,    0x6A, 0,    0x56, 0xE8, -1,   -1,   -1,
                 -1,   0x83, 0xC4, 0x0C, 0xC7, 6,    -1,   -1,   -1,   -1,   0x8D, 0x4E,
                 4,    0xE8, -1,   -1,   -1,   -1,   0xC7, 6,    -1,   -1,   -1,   -1,
                 0x8B, 0xC6, 0x89, 0x35, -1,   -1,   -1,   -1,   0x5E, 0xC3, 0xC7, 5,
                 -1,   -1,   -1,   -1,   0,    0,    0,    0,    0x33, 0xC0, 0x5E, 0xC3});
    const auto* item_slot = absolute(client, items + 1, 2, 4);
    require(absolute(client, items + 0x49, 2, 4) == item_slot &&
                absolute(client, items + 0x51, 2, 4) == item_slot,
            "Item-system factory singleton allocation and lookup operands disagree");
    (void)absolute(client, items + 0xA, 1, 4); // genuine imported g_pMemAlloc variable
    (void)absolute(client, items + 0x33, 1, 4); // base-class vtable during construction
    (void)absolute(client, items + 0x41, 1, 4); // final item-system vtable
    // All relative calls must remain the actual compiled helper identities.
    // No game allocation or constructor executes during binding discovery.
    for (const auto pair : std::array<std::pair<const std::byte*, std::uint32_t>, 3>{
             {{glow + 0x4C, 0xAC74D7u}, {items + 0x29, 0xACB6E0u}, {items + 0x3A, 0x6D1160u}}}) {
        const auto target = reinterpret_cast<std::uintptr_t>(pair.first) + 5 +
                            static_cast<std::intptr_t>(read<std::int32_t>(pair.first + 1));
        const auto* pointer = reinterpret_cast<const std::byte*>(target);
        require(client.contains(pointer, 1, 0) && client.rva(pointer) == pair.second,
                "Client singleton factory has an unsupported helper call");
    }
    // The refresh notifications borrow the address of the engine's lazy
    // CUtlVector pointer. Its null contents remain valid until the real
    // client getter allocates it; discovery never invokes that allocator.
    expect_code(client, client.base + 0x448C4, {0xC7, 5, -1, -1, -1, -1, 0, 0, 0, 0});
    const auto* refresh = absolute(client, client.base + 0x448C6, 2, 4);
    require(client.rva(refresh) == 0x5344CE0,
            "Startup refresh pointer has unsupported client provenance");
    expect_code(client, client.base + 0x418DC0,
                {0x56, 0x33, 0xF6, 0xA1, -1, -1, -1, -1, 0x85, 0xC0, 0x75, 0x79});
    require(absolute(client, client.base + 0x418DC4, 2, 4) == refresh,
            "Startup refresh initializer and notifier name different pointer slots");
    expect_code(client, client.base + 0x418E40,
                {0xA3, -1,   -1, -1,   -1,   0x3B, 0x70, 0x0C, 0x7D, 0x0F, 0x8B, 0,    0x8B, 0x0C,
                 0xB0, 0x8B, 1,  0xFF, 0x10, 0x46, 0xE9, 0x6A, 0xFF, 0xFF, 0xFF, 0x5E, 0xC3});
    require(absolute(client, client.base + 0x418E41, 2, 4) == refresh,
            "Startup refresh notifier does not reload its own vector publication");
    expect_code(client, client.base + 0x418FA0, {0xA1, -1, -1, -1, -1, 0x85, 0xC0, 0x75, 0x49});
    expect_code(
        client, client.base + 0x418FDF,
        {0x89, 0x0D, -1, -1, -1, -1, 0xC3, 0xC7, 5, -1, -1, -1, -1, 0, 0, 0, 0, 0x33, 0xC0, 0xC3});
    require(absolute(client, client.base + 0x418FA1, 2, 4) == refresh &&
                absolute(client, client.base + 0x418FE1, 2, 4) == refresh &&
                absolute(client, client.base + 0x418FE8, 2, 4) == refresh,
            "Startup refresh getter success/failure publications disagree");
    data.native_cell<client::StartupFactory>(0x434678C4u) =
        reinterpret_cast<client::StartupFactory>(const_cast<std::byte*>(glow));
    data.native_cell<client::StartupFactory>(0x43468B9Cu) =
        reinterpret_cast<client::StartupFactory>(const_cast<std::byte*>(items));
    data.native_cell<client::StartupRefreshSource*>(0x43467C04u) =
        reinterpret_cast<client::StartupRefreshSource*>(const_cast<std::byte*>(refresh));
}
} // namespace skeet::engine
