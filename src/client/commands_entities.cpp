#include "skeet/client/commands.hpp"
#include <cstring>

namespace skeet::client {

void save_command_entity_properties(const CommandEntityBindings& bindings,
                                    CommandEntitySnapshot& saved) {
    saved.fill(0);
    for (std::uint32_t index = 1; index <= 64; ++index) {
        void* const entity = engine::invoke<void*>(bindings.entity_list, 3, index);
        if (entity && engine::invoke<bool>(entity, 158)) {
            auto* property = static_cast<std::byte*>(entity) + bindings.overridden_property;
            std::memcpy(&saved[index & 63], property, sizeof(std::uint32_t));
            std::memcpy(property, bindings.replacement_value, sizeof(std::uint32_t));
        }
    }
}

void restore_command_entity_properties(const CommandEntityBindings& bindings,
                                       const CommandEntitySnapshot& saved) {
    for (std::uint32_t index = 1; index <= 64; ++index) {
        if (saved[index & 63] == 0)
            continue;
        void* const entity = engine::invoke<void*>(bindings.entity_list, 3, index);
        if (entity && engine::invoke<bool>(entity, 158)) {
            auto* property = static_cast<std::byte*>(entity) + bindings.overridden_property;
            std::memcpy(property, &saved[index & 63], sizeof(std::uint32_t));
        }
    }
}

} // namespace skeet::client
