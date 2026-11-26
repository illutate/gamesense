#include "skeet/skins/knife_precache.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include <bit>
#include <cstring>

namespace skeet::skins {
namespace {
void* model_precache(void* container) {
    const auto count = engine::invoke<std::int32_t>(container, 5);
    for (std::int32_t index = 0; index < count; ++index) {
        auto* table = engine::invoke<void*>(container, 4, index);
        if (!table)
            continue;
        const auto* name = engine::invoke<const char*>(table, 1);
        if (name && engine::netvar_name_hash(name) == 0x9FE6B136U)
            return table;
    }
    return nullptr;
}
std::int32_t add(std::int32_t left, std::uint32_t right) {
    return std::bit_cast<std::int32_t>(std::uint32_t(left) + right);
}
}
void refresh_knife_precache(SequenceRuntime& state, void* string_tables) {
    auto* table = model_precache(string_tables);
    if (!table)
        return;
    const auto count = engine::invoke<std::int32_t>(table, 3);
    for (std::int32_t index = 0; index < count; index = add(index, 1)) {
        const auto* path = engine::invoke<const char*>(table, 9, index);
        if (!path || std::strlen(path) <= 19)
            continue;
        std::uint32_t prefix;
        std::memcpy(&prefix, path + 15, sizeof(prefix));
        if (prefix != 0x6E6B5F76U)
            continue;
        const auto hash = engine::netvar_name_hash(path);
        const auto definitions = knife_models();
        for (std::size_t knife = 0; knife < definitions.size(); ++knife) {
            if (definitions[knife].view_model_hash != hash)
                continue;
            const auto extra = definitions[knife].native_word_10;
            state.models[knife].entity_model = index;
            state.models[knife].view_model = add(index, 1U + extra);
            state.models[knife].world_model = add(index, 2U + extra);
            index = add(index, 2);
            break;
        }
    }
}
} // namespace skeet::skins
