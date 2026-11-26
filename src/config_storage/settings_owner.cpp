#include "skeet/config_storage/settings_owner.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet {
namespace {
template <class T> T& field(std::byte* storage, std::size_t offset) noexcept {
    return *reinterpret_cast<T*>(storage + offset);
}
void append(ConfigRegistries& configuration, std::byte* storage, std::size_t offset,
            std::uint32_t hash, std::uint16_t size, ConfigType type) noexcept {
    auto& descriptor = field<ConfigDescriptor>(storage, offset);
    descriptor.hash = hash;
    descriptor.serialized_size = size;
    descriptor.type = type;
    append_config_link(configuration.descriptors, descriptor);
}
}

void construct_settings_owner(void* owner, ConfigRegistries& configuration) {
    if (!owner || reinterpret_cast<std::uintptr_t>(owner) % alignof(std::uint32_t))
        throw std::invalid_argument("CONFIG settings storage requires its native alignment");
    auto* const storage = static_cast<std::byte*>(owner);
    initialize_color_config(configuration.descriptors, field<ColorConfig>(storage, 0), 0x141CE87Fu,
                            0xFF2BCA9Fu);
    initialize_hotkey_config(configuration, field<HotkeyConfig>(storage, 0x14), 0x8BBF8EA9u, 0x2Eu,
                             HotkeyMode::hold);
    initialize_integer_config(configuration.descriptors, field<IntegerConfig>(storage, 0x3C),
                              0xD3F1456Eu, 1);
    initialize_integer_config(configuration.descriptors, field<IntegerConfig>(storage, 0x50),
                              0x27BA18FAu, 0);
    append(configuration, storage, 0x64, 0x1F495BA0u, 4, ConfigType::integer);
    append(configuration, storage, 0x78, 0xEAA92CD1u, 4, ConfigType::integer);
    append(configuration, storage, 0x8C, 0x6D868B05u, 0x200, ConfigType::integer_array);
    std::memset(storage + 0x9C, 0, 0x200);
    append(configuration, storage, 0x29C, 0xA340162Au, 4, ConfigType::integer);
    append(configuration, storage, 0x2B0, 0x580B210Eu, 0x54, ConfigType::fixed_array);
    field<std::int32_t>(storage, 0x2C0) = 9;
    append(configuration, storage, 0x314, 0xE44D893Bu, 1, ConfigType::boolean);
}

std::byte* create_settings_owner(engine::NativeDataImage& data, ConfigRegistries& configuration,
                                 engine::JobMemory memory) {
    if (&configuration.descriptors != &data.native_cell<ConfigLink>(0x434798BCu) ||
        &configuration.hotkeys != &data.native_cell<HotkeyLink>(0x434798C4u))
        throw std::invalid_argument(
            "CONFIG settings owner requires the canonical configuration registries");
    auto& active = data.native_cell<std::uint32_t>(0x43478C68u);
    if (active)
        throw std::logic_error("CONFIG settings owner is already constructed");
    if (!memory.allocate || !memory.free || !memory.process_heap)
        throw std::invalid_argument("CONFIG settings owner requires the canonical process heap");
    auto* const owner =
        static_cast<std::byte*>(engine::allocate_owner_storage(memory, settings_owner_size));
    if (!owner)
        throw std::bad_alloc{};
    construct_settings_owner(owner, configuration);
    active = std::uint32_t(reinterpret_cast<std::uintptr_t>(owner));
    return owner;
}
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(ConfigDescriptor) == 0x10);
static_assert(sizeof(HotkeyConfig) == 0x28);
static_assert(sizeof(ScriptHashArrayConfig) == 0x210);
static_assert(sizeof(MenuControlArrayConfig) == 0x64);
#endif
} //namespace skeet
