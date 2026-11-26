#include "skeet/config.hpp"

namespace skeet {

// 0x433AFD92..0x433AFDA6, called by registration wrapper 0x43385AE4.
void append_config_link(ConfigLink& sentinel, ConfigLink& descriptor) noexcept {
    ConfigLink* previous = sentinel.next ? sentinel.previous : &sentinel;
    descriptor.next = &sentinel;
    descriptor.previous = previous;
    previous->next = &descriptor;
    sentinel.previous = &descriptor;
}

// Complete function 0x433E7F74..0x433E7F8D. Duplicates return the first entry.
ConfigLink* find_config(ConfigLink& sentinel, std::uint32_t hash) noexcept {
    for (auto* descriptor = sentinel.next; descriptor != &sentinel; descriptor = descriptor->next)
        if (static_cast<ConfigDescriptor*>(descriptor)->hash == hash)
            return descriptor;
    return nullptr;
}

namespace {
void initialize_descriptor(ConfigLink& registry, ConfigDescriptor& descriptor, std::uint32_t hash,
                           std::uint16_t size, ConfigType type) noexcept {
    descriptor.hash = hash;
    descriptor.serialized_size = size;
    descriptor.type = type;
    append_config_link(registry, descriptor);
}
}

// 0x4333CA03 tail-calls 0x433AFD92 with the separate hotkey sentinel.
void append_hotkey(HotkeyLink& sentinel, RegisteredHotkey& binding) noexcept {
    HotkeyLink* previous = sentinel.next ? sentinel.previous : &sentinel;
    binding.next = &sentinel;
    binding.previous = previous;
    previous->next = &binding;
    sentinel.previous = &binding;
}

// Complete 0x43486649..0x43486677. Bit 0 survives; the toggle bit is cleared.
void initialize_hotkey(HotkeyLink& sentinel, RegisteredHotkey& binding, std::uint32_t key,
                       HotkeyMode mode) noexcept {
    append_hotkey(sentinel, binding);
    binding.callback_object = nullptr;
    binding.callback = nullptr;
    binding.binding.mode = mode;
    binding.binding.packed = (key << 2) | (binding.binding.packed & 1u);
}

// Repeated inline construction, including 0x4348FAE5..0x4348FAF4.
void initialize_boolean_config(ConfigLink& registry, BooleanConfig& descriptor,
                               std::uint32_t hash) noexcept {
    initialize_descriptor(registry, descriptor, hash, 1, ConfigType::boolean);
}

// Complete 0x43482470..0x43482496.
void initialize_integer_config(ConfigLink& registry, IntegerConfig& descriptor, std::uint32_t hash,
                               std::int32_t value) noexcept {
    initialize_descriptor(registry, descriptor, hash, 4, ConfigType::integer);
    descriptor.value = value;
}

// Complete 0x434823C3..0x434823E8; channels retain their native packed order.
void initialize_color_config(ConfigLink& registry, ColorConfig& descriptor, std::uint32_t hash,
                             std::uint32_t value) noexcept {
    initialize_descriptor(registry, descriptor, hash, 4, ConfigType::color);
    descriptor.value = value;
}

// Complete 0x43480690..0x434806BD.
void initialize_integer_array_config(ConfigLink& registry, IntegerArrayConfig& descriptor,
                                     std::uint32_t hash, std::int32_t value) noexcept {
    initialize_descriptor(registry, descriptor, hash, 36, ConfigType::integer_array);
    descriptor.value.fill(value);
}

// Complete 0x434903D1..0x43490400. Only packed key/mode serialize (8 bytes);
// the registered value also owns links and a callback pair before those bytes.
void initialize_hotkey_config(ConfigRegistries& registries, HotkeyConfig& descriptor,
                              std::uint32_t hash, std::uint32_t key, HotkeyMode mode) noexcept {
    initialize_descriptor(registries.descriptors, descriptor, hash, 8, ConfigType::hotkey);
    initialize_hotkey(registries.hotkeys, descriptor.value, key, mode);
}

// Complete 0x434825FB: anonymous, unassigned Escape binding in hold mode.
void initialize_default_hotkey_config(ConfigRegistries& registries,
                                      HotkeyConfig& descriptor) noexcept {
    initialize_hotkey_config(registries, descriptor, 0, 0x1B, HotkeyMode::hold);
}

} // namespace skeet
